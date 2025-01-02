#include "Hypnos/Logging.hpp"
#include "Hypnos/Network/TcpSocket.hpp"
#include <cstring>
#include <liburing.h>
#include <stdexcept>

namespace Blanketmen {
namespace Hypnos {

TcpSocket::TcpSocket()
{
    sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        throw std::runtime_error("[TcpSocket] Failed to create socket. Error: " + std::string(strerror(errno)));
    }

    int opt = 0;
    if (setsockopt(sock_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
    {
        Logging::Error("[TcpSocket] Failed to set socket options.");
        close(sock_fd);
        return;
    }

    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags < 0)
    {
        Logging::Error("[TcpSocket] Failed to get socket flags.");
        close(sock_fd);
        return;
    }

    if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        Logging::Error("[TcpSocket] Failed to set socket flags.");
        close(sock_fd);
        return;
    }
}

TcpSocket::~TcpSocket()
{
    close(sock_fd);
}

void TcpSocket::Listen()
{
    struct sockaddr_in6 sock_addr = {};
    sock_addr.sin6_family = AF_INET6;
    sock_addr.sin6_addr = in6addr_any;
    sock_addr.sin6_port = htons(27015);

    if (bind(sock_fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) < 0)
    {
        Logging::Error("[TcpSocket] Failed to bind socket.");
        close(sock_fd);
        return;
    }

    // TODO: Backlog should be configurable.
    if (listen(sock_fd, 128) < 0)
    {
        Logging::Error("[TcpSocket] Failed to listen on socket.");
        close(sock_fd);
        return;
    }

    Logging::Info("[TcpSocket] Listening on socket.");
    recvThread = new Thread(&TcpSocket::ProcessEvents, this);
    sendThread = new Thread(&TcpSocket::SendResponses, this);
}

void TcpSocket::Connect()
{
}

void TcpSocket::Disconnect()
{
}

void TcpSocket::Receive()
{
}

void TcpSocket::Send()
{
}

void TcpSocket::ProcessEvents()
{
    struct io_uring io_ring;
    io_uring_queue_init(256, &io_ring, 0);

    // 提交初始的 accept 請求
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_prep_accept(sqe, sock_fd, (sockaddr*)&client_addr, &client_len, 0);
    io_uring_submit(&io_ring);

    while (true)
    {
        struct io_uring_cqe* cqe;
        int ret = io_uring_wait_cqe(&io_ring, &cqe);
        if (ret < 0)
        {
            perror("io_uring_wait_cqe failed");
            break;
        }

        if (cqe->res >= 0)
        {
            int client_fd = cqe->res; // 新的客戶端連線
            set_nonblocking(client_fd);

            // 回顯消息
            Client* client = new Client{ client_fd };
            struct io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
            io_uring_prep_recv(sqe, client_fd, client->data, BUFFER_SIZE, 0);
            io_uring_sqe_set_data(sqe, client);
            io_uring_submit(&io_ring);
        }
        else
        {
            Logging::Error("[TcpSocket] io_uring error.");
            //std::cerr << "io_uring error: " << cqe->res << "\n";
        }

        // 檢查是否為接收數據完成
        void* user_data = io_uring_cqe_get_data(cqe);
        if (user_data)
        {
            Client* client = static_cast<Client*>(user_data);
            if (cqe->res > 0)
            {
                //std::cout << "Received: " << std::string(client->buffer, cqe->res) << "\n";

                // Echo back the message
                struct io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
                io_uring_prep_send(sqe, client->fd, client->data, cqe->res, 0);
                io_uring_sqe_set_data(sqe, client);
                io_uring_submit(&io_ring);
            }
            else
            {
                close(client->fd);
                delete client;
            }
        }

        io_uring_cqe_seen(&io_ring, cqe); // 標記完成事件已處理
    }

    io_uring_queue_exit(&io_ring);
    close(sock_fd);
}

void TcpSocket::SendResponses()
{
}

} // namespace Hypnos
} // namespace Blanketmen