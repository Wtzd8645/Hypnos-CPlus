#include "Hypnos/Logging.hpp"
#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/NetworkUtils.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <Hypnos-Core/Thread.hpp>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/unistd.h>

namespace Blanketmen {
namespace Hypnos {

TcpServer::TcpServer()
{
}

TcpServer::~TcpServer()
{
    Release();
}

void TcpServer::Initialize()
{
    if (server_fd >= 0)
    {
        Logging::Error("[TcpSocket] Socket is already initialized.");
        return;
    }

    try
    {
        server_fd = socket(AF_INET6, SOCK_STREAM, 0);
        if (server_fd < 0)
        {
            throw std::runtime_error("[TcpSocket] Failed to create socket. Error: " + std::string(strerror(errno)));
        }

        int opt = 0;
        if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
        {
            throw std::runtime_error("[TcpSocket] Failed to set socket options.");
        }

        NetworkUtils::SetNonBlocking(server_fd);
        if (io_uring_queue_init(256, &recv_ring, 0)) // queue_deep should be configurable.
        {
            throw std::runtime_error("[TcpSocket] Failed to initialize io_uring.");
        }

        if (io_uring_queue_init(256, &send_ring, 0)) // queue_deep should be configurable.
        {
            throw std::runtime_error("[TcpSocket] Failed to initialize io_uring.");
        }
    }
    catch (const std::exception& e)
    {
        Logging::Error("[TcpSocket] Failed to initialize socket. Error: %s", e.what());
        Release();
    }
}

void TcpServer::Release()
{
    Stop();
    if (server_fd >= 0)
    {
        io_uring_queue_exit(&recv_ring);
        io_uring_queue_exit(&send_ring);
        close(server_fd);
        server_fd = -1;
    }
}

void TcpServer::Listen()
{
    sockaddr_in6 sock_addr = { AF_INET6, htons(27015), 0, in6addr_any, 0 };
    if (bind(server_fd, (sockaddr*)&sock_addr, sizeof(sock_addr)) < 0)
    {
        Logging::Error("[TcpSocket] Failed to bind socket.");
        return;
    }

    // TODO: Backlog should be configurable.
    if (listen(server_fd, 256) < 0)
    {
        Logging::Error("[TcpSocket] Failed to listen on socket.");
        return;
    }

    Logging::Info("[TcpSocket] Listening on socket.");
    running = true;
    recv_thread = new Thread(&TcpServer::ProcessRecvEvents, this);
    send_thread = new Thread(&TcpServer::ProcessSendEvents, this);
}

void TcpServer::Stop()
{
    if (running)
    {
        running = false;
        if (recv_thread->joinable())
        {
            recv_thread->join();
        }
        delete recv_thread;

        if (send_thread->joinable())
        {
            send_thread->join();
        }
        delete send_thread;
    }
}

void TcpServer::Dispatch()
{
}

void TcpServer::ProcessRecvEvents()
{
    try
    {
        Accept();
        while (running)
        {
            io_uring_cqe* cqe;
            int result = io_uring_wait_cqe(&recv_ring, &cqe);
            if (result < 0)
            {
                HandleCqeError(-result);
                continue;
            }

            Connection* conn = static_cast<Connection*>(io_uring_cqe_get_data(cqe));
            if (conn == nullptr)
            {
                Logging::Error("[TcpSocket] Connection is null.");
            }
            else
            {
                if (conn == &accept_conn)
                {
                    HandleAccept(cqe->res);
                }
                else
                {
                    HandleReceive(conn, cqe->res);
                }
            }
            io_uring_cqe_seen(&recv_ring, cqe);
        }
    }
    catch (const std::exception& e)
    {
        Logging::Error("[TcpSocket] Failed to process events. Error: %s", e.what());
        Release();
    }
}

void TcpServer::ProcessSendEvents()
{
    try
    {
        while (running)
        {
            io_uring_cqe* cqe;
            int result = io_uring_wait_cqe(&send_ring, &cqe);
            if (result < 0)
            {
                HandleCqeError(-result);
                continue;
            }

            Connection* conn = static_cast<Connection*>(io_uring_cqe_get_data(cqe));
            if (conn == nullptr)
            {
                Logging::Error("[TcpSocket] Connection is null.");
            }
            else
            {
                HandleSend(conn, cqe->res);
            }
            io_uring_cqe_seen(&send_ring, cqe);
        }
    }
    catch (const std::exception& e)
    {
        Logging::Error("[TcpSocket] Failed to process send events. Error: %s", e.what());
        Release();
    }
}

inline void TcpServer::Accept()
{
    io_uring_sqe* sqe = io_uring_get_sqe(&recv_ring);
    io_uring_sqe_set_data(sqe, &accept_conn);
    io_uring_prep_accept(sqe, server_fd, (sockaddr*)&accept_conn.addr, &accept_conn.addr_len, 0);
    io_uring_submit(&recv_ring);
}

inline void TcpServer::HandleAccept(int result)
{
    if (result >= 0)
    {
        NetworkUtils::SetNonBlocking(result);
        Connection* conn = connectionMap.find(result)->second;
        if (conn == nullptr)
        {
            conn = new Connection;
            connectionMap[result] = conn;
        }

        conn->sock = result;
        conn->addr = accept_conn.addr;
        conn->addr_len = accept_conn.addr_len;
        Receive(conn);
        Accept();
        return;
    }

    switch (-result)
    {
        case EAGAIN:
        case ECONNABORTED:
        {
            Accept();
            break;
        }
        case ENOMEM:
        case ENFILE:
        case EMFILE:
        case ENOBUFS:
        {
            Logging::Error("[TcpSocket] Failed to accept connection.");
            usleep(1000);
            Accept();
            break;
        }
        case EBADF:
        case EFAULT:
        case EINVAL:
        case ENOTSOCK:
        {
            Logging::Error("[TcpSocket] Fatal accept error.");
            break;
        }
        default:
        {
            Logging::Error("[TcpSocket] Unknown accept error.");
            Accept();
            break;
        }
    }
}

inline void TcpServer::Receive(Connection* conn)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&recv_ring);
    io_uring_prep_recv(sqe, conn->sock, conn->recvCtx.buffer.data, 1024, 0);
    io_uring_sqe_set_data(sqe, conn);
    io_uring_submit(&recv_ring);
}

inline void TcpServer::HandleReceive(Connection* conn, int result)
{
    if (result > 0)
    {
        Logging::Info("[TcpSocket] Received %d bytes.", result);
        // TODO: Process received data.
        Receive(conn);
        return;
    }

    switch (-result)
    {
        case EAGAIN:
        {
            Receive(conn);
            break;
        }
        case 0:
        case ECONNRESET:
        {
            Logging::Info("[TcpSocket] Connection closed. Socket: %d", conn->sock);
            close(conn->sock);
            break;
        }
        default:
        {
            Logging::Error("[TcpSocket] Failed to receive data. Socket: %d", conn->sock);
            close(conn->sock);
            break;
        }
    }
}

void TcpServer::Send(ResponseBase* resp)
{
    for (auto conn : resp->conns)
    {
        io_uring_sqe* sqe = io_uring_get_sqe(&send_ring);
        io_uring_prep_send(sqe, conn->sock, resp->buffer.data, resp->buffer.offset, 0);
        io_uring_sqe_set_data(sqe, conn);
    }
    io_uring_submit(&send_ring);
}

inline void TcpServer::HandleSend(Connection* conn, int result)
{
}

} // namespace Hypnos
} // namespace Blanketmen