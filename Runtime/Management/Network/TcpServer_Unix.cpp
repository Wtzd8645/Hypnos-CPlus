#include "Hypnos/Logging.hpp"
#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <Hypnos-Core/Memory/Memory.hpp>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {

TcpServer::TcpServer(io_uring_context& ctx, size_t max_conns) : SocketServerBase(ctx),
    connection_pool(max_conns),
    conn_events(8192),
    conn_event_handlers(2),
    requests(8192),
    request_pool(nullptr),
    socket_op_args(8192),
    response_pool(nullptr)
{
}

TcpServer::~TcpServer()
{
    Stop();
    delete request_pool;
}

void TcpServer::Start()
{
    if (sock_fd <= INVALID_FD)
    {
        Logging::Error("[TcpSocket] Socket is not initialized or already running.");
        return;
    }

    sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        Logging::Error("[TcpSocket] Failed to create socket.");
        return;
    }

    int opt = 0;
    if (setsockopt(sock_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
    {
        Logging::Error("[TcpSocket] Failed to set socket options.");
        return;
    }

    //setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)); // TODO: Make configurable.
    //setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)); // TODO: Make configurable.
    
    sockaddr_in6 sock_addr = { AF_INET6, htons(27015), 0, in6addr_any, 0 };
    if (bind(sock_fd, (sockaddr*)&sock_addr, sizeof(sock_addr)) < 0)
    {
        Logging::Error("[TcpSocket] Failed to bind socket.");
        return;
    }

    if (listen(sock_fd, 256) < 0) // TODO: Make configurable.
    {
        Logging::Error("[TcpSocket] Failed to listen on socket.");
        return;
    }

    Logging::Info("[TcpSocket] Listening on socket.");
    io_event_args* acpt_args = io_ctx.event_args_pool.Pop();
    acpt_args->op = ACPT;
    AcceptInternal(acpt_args);

    io_event_args* poll_args = io_ctx.event_args_pool.Pop();
    poll_args->op = POLL;
    PollInternal(poll_args);
}

void TcpServer::Stop()
{
    if (sock_fd <= INVALID_FD)
    {
        Logging::Error("[TcpSocket] Socket is not running.");
        return;
    }

    Logging::Info("[TcpSocket] Closing all active connections");
    for (auto& conn : connection_pool)
    {
        if (conn.sock_fd > INVALID_FD)
        {
            CloseInternal(&conn);
        }
    }
    connection_pool.Clear();

    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);
    sock_fd = INVALID_FD;
}

void TcpServer::Dispatch()
{
    /*ConnectionEvent evt;
    while (conn_events.Dequeue(evt))
    {
        auto it = conn_event_handlers.find(evt.evtId);
        if (it != conn_event_handlers.end())
        {
            it->second->Handle(evt.conn);
        }
    }*/

    RequestBase* req = nullptr;
    while (requests.Dequeue(req)) // TODO: Can batch pop.
    {
        EventHandlerBase<RequestBase*>*& handler = request_handlers[req->gid];
        if (handler != nullptr)
        {
            handler->Handle(req);
        }
        request_pool->Release(req);
    }

    // PERF: Maybe change back to submit and wait.
    if (io_ctx.ring.sq.kflags[0] & IORING_SQ_NEED_WAKEUP)
    {
        io_uring_enter(io_ctx.ring.ring_fd, 0, 0, IORING_ENTER_SQ_WAKEUP, NULL);
    }
}

void TcpServer::Close(Container::List<ConnectionHandle>* conn_handles)
{
    SocketOperationArgs args = { conn_handles, nullptr };

    while (!socket_op_args.Enqueue(args))
    {
        std::this_thread::yield();
    }
    
    if (polling.fetch_add(1, std::memory_order_release) == 0)
    {
        eventfd_write(io_ctx.efd, 1);
    }
}

void TcpServer::Send(Container::List<ConnectionHandle>* conn_handles, ResponseBase* resp)
{
    SocketOperationArgs args = { conn_handles, io_ctx.send_buf_pool.Pop() };
    resp->Pack(args);
    response_pool->Release(resp);

    while (!socket_op_args.Enqueue(args))
    {
        std::this_thread::yield();
    }

    if (polling.fetch_add(1, std::memory_order_release) == 0)
    {
        eventfd_write(io_ctx.efd, 1);
    }
}

void TcpServer::ProcessEvent(io_event_args* args, int32 res, uint32 flags)
{
    switch (args->op)
    {
        case ACPT: { OnAccept(args, res, flags); break; }
        case RECV: { OnReceive(args, res, flags); break; }
        case POLL: { OnPoll(args, res, flags); break; }
        case SEND: { OnSend(args, res, flags); break; }
        default: { Logging::Error("[TcpSocket] Unknown operation type."); break; }
    }
}

void TcpServer::CloseInternal(Connection* conn)
{
    if (conn->sock_fd <= INVALID_FD)
    {
        return;
    }

    shutdown(conn->sock_fd, SHUT_RDWR);
    close(conn->sock_fd);
    conn->sock_fd = INVALID_FD;
    conn->version++;

    recv_context& recv_ctx = conn->recv_ctx;
    recv_ctx.packet_bytes = 0;
    recv_ctx.waiting_bytes = sizeof(packet_size);
    recv_ctx.received_bytes = 0;

    send_context& send_ctx = conn->send_ctx;
    send_ctx.pending_bytes = 0;
    send_ctx.processed_bytes = 0;
    uint8* buf = send_ctx.buffer;
    while (buf != nullptr)
    {
        auto& send_meta = buffer_metadata::get(buf, BUF_META_OFFSET).send;
        if (--send_meta.conn_count == 0)
        {
            io_ctx.send_buf_pool.Push(buf);
        }

        if (send_ctx.pending_responses.empty())
        {
            buf = nullptr;
        }
        else
        {
            buf = send_ctx.pending_responses.front();
            send_ctx.pending_responses.pop();
        }
    }

    connection_pool.Release(conn);
}

inline void TcpServer::PollInternal(io_event_args* args)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_poll_multishot(sqe, io_ctx.efd, POLLIN);
}

inline void TcpServer::AcceptInternal(io_event_args* args)
{
    static sockaddr_storage acpt_addr;
    static socklen_t acpt_addr_len = sizeof(sockaddr_storage);

    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_multishot_accept(sqe, sock_fd, (sockaddr*)&acpt_addr, &acpt_addr_len, 0);
}

inline void TcpServer::ReceiveInternal(io_event_args* args)
{
    args->conn_ver = args->conn->version;
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    sqe->buf_group = IO_RECV_BUF_GROUP;
    io_uring_prep_recv_multishot(sqe, args->conn->sock_fd, nullptr, 0, 0);
}

inline void TcpServer::SendInternal(io_event_args* args, const void* buf, int32 len)
{
    // TODO: When packet exceeds threshold, use io_uring_prep_send_zc?
    args->conn_ver = args->conn->version;
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_send(sqe, args->conn->sock_fd, buf, len, 0);
}

void TcpServer::OnPoll(io_event_args* args, int32 res, uint32 flags)
{
    uint8 expected;
    SocketOperationArgs op_args;
    do
    {
        expected = polling.load(std::memory_order_acquire);
        while (socket_op_args.Dequeue(op_args)) // TODO: Can batch pop.
        {
            if (op_args.buffer == nullptr)
            {
                for (ConnectionHandle& conn_handle : *(op_args.conn_handles))
                {
                    CloseInternal(conn_handle);
                }
            }
            else
            {
                auto& send_meta = buffer_metadata::get(op_args.buffer, BUF_META_OFFSET).send;
                send_meta.conn_count = op_args.conn_handles->size();
                send_meta.size = op_args.length;
                for (ConnectionHandle& conn_handle : *(op_args.conn_handles))
                {
                    if (conn_handle.version != conn_handle.conn->version)
                    {
                        send_meta.conn_count--;
                        continue;
                    }

                    send_context& ctx = conn_handle.conn->send_ctx;
                    if (ctx.pending_bytes > 0)
                    {
                        // PERF: Merge packets if size less than threshold.
                        ctx.pending_responses.push(op_args.buffer);
                    }
                    else
                    {
                        io_event_args* args = io_ctx.event_args_pool.Pop();
                        args->op = SEND;
                        args->conn = conn_handle.conn;
                        ctx.buffer = op_args.buffer;
                        ctx.pending_bytes = send_meta.size;
                        ctx.processed_bytes = 0;
                        SendInternal(args, ctx.buffer, send_meta.size);
                    }
                }

                if (send_meta.conn_count <= 0)
                {
                    io_ctx.send_buf_pool.Push(op_args.buffer);
                }
            }
        }
    }
    while (polling.compare_exchange_weak(expected, 0, std::memory_order_relaxed));

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        if (res < 0)
        {
            Logging::Error("[TcpSocket] Failed to poll. Error: %s", strerror(-res));
            io_ctx.event_args_pool.Push(args);
        }
        else
        {
            PollInternal(args);
        }
    }
}

void TcpServer::OnAccept(io_event_args* args, int32 res, uint32 flags)
{
    static const std::unordered_set<int> fatal_errors =
    {
        EPERM, EBADF, EFAULT, EINVAL, ENOTSOCK, EOPNOTSUPP,
        EAFNOSUPPORT, EADDRINUSE, EADDRNOTAVAIL,
        ENETDOWN, ESHUTDOWN
    };

    if (res >= 0)
    {
        io_event_args* recv_args = io_ctx.event_args_pool.Pop();
        recv_args->op = RECV;
        recv_args->conn = connection_pool.Aquire(res);
        ReceiveInternal(recv_args);
    }

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        if (res >= 0)
        {
            AcceptInternal(args);
            return;
        }

        if (fatal_errors.contains(-res))
        {
            Logging::Error("[TcpSocket] Failed to accept connection. Error: %s", strerror(-res));
            io_ctx.event_args_pool.Push(args);
            return;
        }

        Logging::Warning("[TcpSocket] Failed to accept connection. Error: %s", strerror(-res));
        std::this_thread::yield();
        AcceptInternal(args);
    }
}

void TcpServer::OnReceive(io_event_args* args, int32 res, uint32 flags)
{
    if (args->conn_ver != args->conn->version)
    {
        if ((flags & IORING_CQE_F_BUFFER) != 0)
        {
            int32 bid = flags >> IORING_CQE_BUFFER_SHIFT;
            uint8* buf = io_ctx.recv_buf_pool[bid];
            io_uring_buf_ring_add(io_ctx.recv_buf_ring, buf, MAX_BUFFER_SIZE, bid, io_ctx.recv_buf_mask, io_ctx.recv_buf_count++);
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            io_ctx.event_args_pool.Push(args);
        }
    }
    else if (res <= 0)
    {
        Logging::Info("[TcpSocket] Connection closed. Socket: %d, Error: %s", args->conn->sock_fd, strerror(-res));
        CloseInternal(args->conn);

        if ((flags & IORING_CQE_F_BUFFER) != 0)
        {
            int32 bid = flags >> IORING_CQE_BUFFER_SHIFT;
            uint8* buf = io_ctx.recv_buf_pool[bid];
            io_uring_buf_ring_add(io_ctx.recv_buf_ring, buf, MAX_BUFFER_SIZE, bid, io_ctx.recv_buf_mask, io_ctx.recv_buf_count++);
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            io_ctx.event_args_pool.Push(args);
        }
    }
    else
    {
        packet_size received_bytes = res;
        packet_size processed_bytes = 0;
        if ((flags & IORING_CQE_F_BUFFER) != 0)
        {
            Logging::Info("[TcpSocket] Received %d bytes.", res);
            int32 bid = flags >> IORING_CQE_BUFFER_SHIFT;
            uint8* buf = io_ctx.recv_buf_pool[bid];

            recv_context& ctx = args->conn->recv_ctx;
            if (ctx.received_bytes > 0)
            {
                if (ctx.waiting_bytes > received_bytes)
                {
                    std::memcpy(ctx.buffer + ctx.received_bytes, buf, received_bytes);
                    ctx.waiting_bytes -= received_bytes;
                    ctx.received_bytes += received_bytes;
                    io_uring_buf_ring_add(io_ctx.recv_buf_ring, buf, MAX_BUFFER_SIZE, bid, io_ctx.recv_buf_mask, io_ctx.recv_buf_count++);
                    return;
                }

                received_bytes -= ctx.waiting_bytes;
                processed_bytes = ctx.waiting_bytes;
                std::memcpy(ctx.buffer + ctx.received_bytes, buf, ctx.waiting_bytes);
                ctx.received_bytes = 0;

                if (ctx.packet_bytes == 0)
                {
                    std::memcpy(&ctx.packet_bytes, ctx.buffer, sizeof(packet_size));
                    if (ctx.packet_bytes <= 0 || ctx.packet_bytes > MAX_PACKET_SIZE)
                    {
                        received_bytes = -1;
                        CloseInternal(args->conn);
                    }
                    else
                    {
                        ctx.waiting_bytes = ctx.packet_bytes;
                    }
                }
                else
                {
                    RequestBase* req = request_pool->Acquire(ctx.buffer);
                    req->conn_handle = { args->conn, args->conn_ver };
                    while (!requests.Enqueue(req))
                    {
                        std::this_thread::yield();
                    }

                    ctx.packet_bytes = 0;
                    ctx.waiting_bytes = sizeof(packet_size);
                }
            }

            while (ctx.waiting_bytes <= received_bytes)
            {
                if (ctx.packet_bytes == 0)
                {
                    std::memcpy(&ctx.packet_bytes, buf + processed_bytes, sizeof(packet_size));
                    if (ctx.packet_bytes <= 0 || ctx.packet_bytes > MAX_PACKET_SIZE)
                    {
                        received_bytes = -1;
                        CloseInternal(args->conn);
                        break;
                    }

                    ctx.waiting_bytes = ctx.packet_bytes;
                    received_bytes -= sizeof(packet_size);
                    processed_bytes += sizeof(packet_size);
                }
                else
                {
                    RequestBase* req = request_pool->Acquire(buf + processed_bytes);
                    req->conn_handle = { args->conn, args->conn_ver };
                    while (!requests.Enqueue(req))
                    {
                        std::this_thread::yield();
                    }

                    received_bytes -= ctx.waiting_bytes;
                    processed_bytes += ctx.waiting_bytes;
                    ctx.packet_bytes = 0;
                    ctx.waiting_bytes = sizeof(packet_size);
                }
            }

            if (received_bytes > 0)
            {
                ctx.received_bytes = received_bytes;
                std::memcpy(ctx.buffer, buf + processed_bytes, received_bytes);
            }

            io_uring_buf_ring_add(io_ctx.recv_buf_ring, buf, MAX_BUFFER_SIZE, bid, io_ctx.recv_buf_mask, io_ctx.recv_buf_count++);
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            if (received_bytes < 0)
            {
                io_ctx.event_args_pool.Push(args);
            }
            else
            {
                ReceiveInternal(args);
            }
        }
    }
}

void TcpServer::OnSend(io_event_args* args, int32 res, uint32 flags)
{
    if (args->conn_ver != args->conn->version || res < 0)
    {
        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            io_ctx.event_args_pool.Push(args);
        }
    }
    else if (res < 0)
    {
        Logging::Info("[TcpSocket] Connection closed while sending. Op: %d, Error: %s)", args->conn->sock_fd, strerror(-res));
        CloseInternal(args->conn); // PERF: Check if error is fatal.

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            io_ctx.event_args_pool.Push(args);
        }
    }
    else
    {
        send_context& ctx = args->conn->send_ctx;
        if ((flags & IORING_CQE_F_NOTIF) == 0)
        {
            Logging::Info("[TcpSocket] Sent %d bytes.", res);
            ctx.pending_bytes -= res;
            ctx.processed_bytes += res;
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            if (ctx.pending_bytes > 0)
            {
                SendInternal(args, ctx.buffer + ctx.processed_bytes, ctx.pending_bytes);
                return;
            }

            auto& send_meta = buffer_metadata::get(ctx.buffer, BUF_META_OFFSET).send;
            if (--send_meta.conn_count <= 0)
            {
                io_ctx.send_buf_pool.Push(ctx.buffer);
            }

            if (ctx.pending_responses.empty())
            {
                ctx.buffer = nullptr;
                ctx.pending_bytes = 0;
                io_ctx.event_args_pool.Push(args);
            }
            else
            {
                ctx.buffer = ctx.pending_responses.front();
                ctx.pending_bytes = buffer_metadata::get(ctx.buffer, BUF_META_OFFSET).send.size;
                ctx.processed_bytes = 0;
                ctx.pending_responses.pop();
                SendInternal(args, ctx.buffer, ctx.pending_bytes);
            }
        }
    }
}

} // namespace Hypnos
} // namespace Blanketmen