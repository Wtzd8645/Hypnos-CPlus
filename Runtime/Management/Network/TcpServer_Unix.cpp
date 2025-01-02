#include "Hypnos/Logging.hpp"
#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/NetworkUtils.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <Hypnos-Core/Thread.hpp>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {

TcpServer::TcpServer() : events(1024), requests(8192), request_factory(nullptr) { }

TcpServer::~TcpServer()
{
    Release();
    delete request_factory;
}

void TcpServer::Initialize()
{
    if (sock >= 0)
    {
        Logging::Error("[TcpSocket] Socket is already initialized.");
        return;
    }

    try
    {
        sock = socket(AF_INET6, SOCK_STREAM, 0);
        if (sock < 0)
        {
            throw std::runtime_error("[TcpSocket] Failed to create socket. Error: " + std::string(strerror(errno)));
        }

        int opt = 0;
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
        {
            throw std::runtime_error("[TcpSocket] Failed to set socket options.");
        }

        NetworkUtils::SetNonBlocking(sock);

        sockaddr_in6 sock_addr = { AF_INET6, htons(27015), 0, in6addr_any, 0 };
        if (bind(sock, (sockaddr*)&sock_addr, sizeof(sock_addr)) < 0)
        {
            Logging::Error("[TcpSocket] Failed to bind socket.");
            return;
        }

        io_uring_params io_params;
        memset(&io_params, 0, sizeof(io_params));
        io_params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN; // TODO: Make configurable.
        if (io_uring_queue_init_params(512, &io_ring, &io_params) < 0)
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
    if (running)
    {
        running = false;
        for (auto& pair : connection_map)
        {
            Close(pair.second);
        }

        if (io_thread->joinable())
        {
            io_thread->join();
        }
    }

    if (sock >= 0)
    {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        sock = -1;
        io_uring_queue_exit(&io_ring);
    }
}

void TcpServer::Listen()
{
    if (sock < 0 || running)
    {
        Logging::Error("[TcpSocket] Socket is not initialized or already running.");
        return;
    }

    if (listen(sock, 256) < 0) // TODO: Backlog should be configurable.
    {
        Logging::Error("[TcpSocket] Failed to listen on socket.");
        return;
    }

    Logging::Info("[TcpSocket] Listening on socket.");
    running = true;
    Accept();
    io_thread = std::make_unique<Thread>(&TcpServer::ProcessEvents, this);
}

void TcpServer::Dispatch()
{
    ConnectionEvent evt;
    while (events.Dequeue(evt))
    {
        auto it = event_handlers.find(evt.sockId);
        if (it != event_handlers.end())
        {
            it->second->Handle(evt);
        }
    }

    RequestBase* req = nullptr;
    while (requests.Dequeue(req))
    {
        EventDispatcher<uint16, RequestBase*>::Dispatch(req->header.msgId, req);
        delete req; // TODO: Change to recycle to thread-safe object pool.
    }
}

inline void TcpServer::Close(Connection* conn)
{
    connection_map.erase(conn->sock); // TODO: Optimize close operation.
    shutdown(conn->sock, SHUT_RDWR);
    close(conn->sock);
    // Notify event handlers.
}

inline void TcpServer::Accept()
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_sqe_set_data(sqe, &accept_conn);
    io_uring_prep_multishot_accept(sqe, sock, (sockaddr*)&accept_conn.addr, &accept_conn.addr_len, 0);
}

inline void TcpServer::Receive(Connection* conn)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_sqe_set_data(sqe, conn);
    io_uring_prep_recv_multishot(sqe, conn->sock, conn->recv_ctx.buffer, 1024, 0); // TODO: Buffer size should be configurable.
}

void TcpServer::Send(ResponseBase* resp)
{
    for (Connection* conn : resp->conns)
    {
        io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
        io_uring_sqe_set_data(sqe, conn);
        io_uring_prep_send(sqe, conn->sock, resp->buffer, resp->offset, 0);
    }
    io_uring_submit(&io_ring);
}

void TcpServer::ProcessEvents()
{
    const int batch_size = 1024;
    io_uring_cqe* cqes[batch_size];

    try
    {
        while (running)
        {
            int res = io_uring_peek_batch_cqe(&io_ring, cqes, batch_size);
            if (res < 0)
            {
                OnCqeError(-res);
                continue;
            }

            for (int i = 0; i < res; ++i)
            {
                io_uring_cqe* cqe = cqes[i];
                EventData* data = static_cast<EventData*>(io_uring_cqe_get_data(cqe));
                switch (data->op)
                {
                    case Operation::ACPT: { OnAccept(data->conn, cqe->res, cqe->flags); break; }
                    case Operation::RECV: { OnReceive(data->conn, cqe->res, cqe->flags); break; }
                    case Operation::SEND: { OnSend(data->conn, cqe->res); break; }
                    default: { Logging::Error("[TcpSocket] Unknown operation type."); break; }
                }
                io_uring_cqe_seen(&io_ring, cqes[i]);
            }

            if (io_uring_submit_and_wait(&io_ring, 1) < 0)
            {
                Logging::Error("[TcpSocket] io_uring_submit_and_wait error during batch processing.");
            }
        }
    }
    catch (const std::exception& e)
    {
        Logging::Error("[TcpSocket] Failed to process events. Error: %s", e.what());
        Release();
    }
}

inline void TcpServer::OnCqeError(int err)
{
    static const std::unordered_set<int> retriable_errors = { EIO, EAGAIN, ENOMEM, EBUSY };
    static const std::unordered_set<int> fatal_errors = { ENXIO, EBADF, EFAULT, EINVAL, ENOSPC };

    if (retriable_errors.contains(err))
    {
        Logging::Warning("[TcpSocket] Failed to peek CQE. Error: %s", strerror(errno));
        usleep(32); // TODO: Make configurable.
        return;
    }

    if (fatal_errors.contains(err))
    {
        throw std::runtime_error("[TcpSocket] Fatal CQE error. Error: " + std::string(strerror(errno)));
    }

    throw std::runtime_error("[TcpSocket] Unknown CQE error. Error: " + std::string(strerror(errno)));
}

inline void TcpServer::OnAccept(Connection* conn, int32 res, int32 flags)
{
    if (res < 0)
    {
        OnAcceptError(conn, -res);
    }
    else
    {
        OnAcceptSuccess(conn, res);
    }

    if (flags & IORING_CQE_F_MORE)
    {
        Accept();
    }
}

inline void TcpServer::OnAcceptSuccess(Connection* conn, int32 res)
{
    try
    {
        NetworkUtils::SetNonBlocking(res);
        auto it = connection_map.find(res);
        Connection* conn = (it != connection_map.end()) ? it->second : nullptr;
        if (conn == nullptr)
        {
            conn = new Connection;
            connection_map[res] = conn;
        }

        conn->sock = res;
        conn->addr = accept_conn.addr;
        conn->addr_len = accept_conn.addr_len;
        Receive(conn);
    }
    catch (const std::exception& e)
    {
        Logging::Error("[TcpSocket] Failed to process accepted connection. Error: %s", e.what());
    }
}

inline void TcpServer::OnAcceptError(Connection* conn, int32 err)
{
    static const std::unordered_set<int> retriable_errors = { EAGAIN, ECONNABORTED };
    static const std::unordered_set<int> resource_errors = { ENOMEM, ENFILE, EMFILE, ENOBUFS };
    static const std::unordered_set<int> fatal_errors = { EBADF, EFAULT, EINVAL, ENOTSOCK };

    if (retriable_errors.contains(err))
    {
        return;
    }

    if (resource_errors.contains(err))
    {
        Logging::Error("[TcpSocket] Resource limit reached while accepting connection.");
        usleep(32); // TODO: Make configurable.
        return;
    }

    if (fatal_errors.contains(err))
    {
        throw std::runtime_error("[TcpSocket] Fatal accept error." + std::string(strerror(errno)));
    }

    Logging::Error("[TcpSocket] Unknown accept error.");
}

inline void TcpServer::OnReceive(Connection* conn, int32 res, int32 flags)
{
    if (res <= 0)
    {
        OnReceiveError(conn, -res);
        return;
    }

    try
    {
        OnReceiveSuccess(conn, res);
        if (flags & IORING_CQE_F_MORE)
        {
            Receive(conn);
        }
    }
    catch (const std::exception& e)
    {
        Logging::Error("[TcpSocket] Failed to process received data. Error: %s", e.what());
        Close(conn);
    }
}

inline void TcpServer::OnReceiveSuccess(Connection* conn, int32 res)
{
    Logging::Info("[TcpSocket] Received %d bytes.", res);
    PacketContext& ctx = conn->recv_ctx;
    ctx.buffer_bytes += res;
    while (ctx.buffer_bytes >= ctx.pending_bytes)
    {
        if (ctx.packet_bytes == 0)
        {
            ctx.packet_bytes = *reinterpret_cast<PacketLengthSize*>(ctx.buffer + ctx.processed_bytes);
            ctx.pending_bytes = ctx.packet_bytes;
            ctx.buffer_bytes -= sizeof(PacketLengthSize);
            ctx.processed_bytes += sizeof(PacketLengthSize);
            continue;
        }

        RequestBase* req = request_factory->Create(ctx.buffer + ctx.processed_bytes, conn); // TODO: Use thread-safe object pool.
        if (req == nullptr)
        {
            Logging::Error("[TcpSocket] Unknown request.");
            Close(conn);
            return;
        }

        requests.Enqueue(req);
        ctx.buffer_bytes -= ctx.pending_bytes;
        ctx.processed_bytes += ctx.pending_bytes;
        ctx.packet_bytes = 0;
        ctx.pending_bytes = sizeof(PacketLengthSize);
    }

    if (ctx.buffer_bytes > 0)
    {
        memmove(ctx.buffer, ctx.buffer + ctx.processed_bytes, ctx.buffer_bytes);
        ctx.processed_bytes = 0;
    }
}

inline void TcpServer::OnReceiveError(Connection* conn, int32 err)
{
    static const std::unordered_set<int> retriable_errors = { EAGAIN };
    static const std::unordered_set<int> connection_closed_errors = { 0, ECONNRESET, ETIMEDOUT };

    if (retriable_errors.contains(err))
    {
        return;
    }

    if (connection_closed_errors.contains(err))
    {
        Logging::Info("[TcpSocket] Connection closed. Socket: %d, Error: %s", conn->sock, strerror(err));
        Close(conn);
        return;
    }

    Logging::Error("[TcpSocket] Failed to receive data. Socket: %d", conn->sock);
    Close(conn);
}

inline void TcpServer::OnSend(Connection* conn, int32 res)
{
    if (res <= 0)
    {
        OnSendError(conn, -res);
        return;
    }

    try
    {
        OnSendSuccess(conn, res);
    }
    catch (const std::exception& e)
    {
        Logging::Error("[TcpSocket] Failed to process send data. Error: %s", e.what());
        Close(conn);
    }
}

inline void TcpServer::OnSendSuccess(Connection* conn, int32 res)
{
    Logging::Info("[TcpSocket] Sent %d bytes.", res);
    PacketContext& ctx = conn->send_ctx;
    if (ctx.buffer_bytes > res)
    {
        ctx.buffer_bytes -= res;
        ctx.processed_bytes += res;

        io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
        io_uring_sqe_set_data(sqe, conn);
        io_uring_prep_send(sqe, conn->sock, ctx.buffer + ctx.processed_bytes, ctx.buffer_bytes, 0);
        return;
    }

    // TODO: Decrease response reference res.
    // TODO: Check if there is response to send.
}

inline void TcpServer::OnSendError(Connection* conn, int32 err)
{
    static const std::unordered_set<int> retriable_errors = { EAGAIN };
    static const std::unordered_set<int> connection_closed_errors = { 0, ECONNRESET, EPIPE, ETIMEDOUT };

    if (retriable_errors.contains(err))
    {
        Logging::Warning("[TcpSocket] Send temporarily failed, will retry. Socket: %d", conn->sock);
        return;
    }
    
    if (connection_closed_errors.contains(err))
    {
        Logging::Info("[TcpSocket] Connection closed while sending. Socket: %d, Error: %s)", conn->sock, strerror(err));
        Close(conn);
        return;
    }

    Logging::Error("[TcpSocket] Send failed. Socket: %d", conn->sock);
    Close(conn);
}

} // namespace Hypnos
} // namespace Blanketmen