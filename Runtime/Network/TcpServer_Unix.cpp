#include "Hypnos/Network/NetworkDefs.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <liburing.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

TcpServer::TcpServer(SocketConfig& cfg, IOContext& ctx) : ServerSocketBase(cfg, ctx) { }

TcpServer::~TcpServer()
{
    Stop();
    delete request_allocator;
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
    IOEventArgs* args = event_args_pool.Acquire();
    args->op = SocketOp::Poll;
    args->sock_ver = version;
    args->sock_id = id;
    PollInternal(args);

    args = event_args_pool.Acquire();
    args->op = SocketOp::Accept;
    args->sock_ver = version;
    args->sock_id = id;
    AcceptInternal(args);
}

void TcpServer::Stop()
{
    if (sock_fd <= INVALID_FD)
    {
        Logging::Error("[TcpSocket] Socket is not running.");
        return;
    }

    Logging::Info("[TcpSocket] Closing all active connections");
    for (auto& conn : conntions)
    {
        if (conn.sock_fd > INVALID_FD)
        {
            CloseInternal(&conn);
        }
    }
    conntions.Clear();

    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);
    sock_fd = INVALID_FD;
}

void TcpServer::Dispatch()
{
    ServerSocketEvent evt;
    while (sock_events.Dequeue(evt))
    {
        conn_event_handlers[static_cast<size_t>(evt.type)]->Handle(evt.conn_handle);
    }

    RequestBase* req = nullptr;
    while (requests.Dequeue(req)) // PERF: Can batch pop.
    {
        EventHandlerBase<RequestBase*>*& handler = request_handlers[req->gid];
        if (handler != nullptr)
        {
            handler->Handle(req);
        }
        request_allocator->Release(req);
    }

    // PERF: Maybe change back to submit and wait.
    if (io_ctx.ring.sq.kflags[0] & IORING_SQ_NEED_WAKEUP)
    {
        io_uring_enter(io_ctx.ring.ring_fd, 0, 0, IORING_ENTER_SQ_WAKEUP, NULL);
    }
}

void TcpServer::Close(List<ConnectionHandle>* conn_handles)
{
    ResponseArgs args = { conn_handles, nullptr, 0 };
    while (!response_args.Enqueue(args))
    {
        std::this_thread::yield();
    }
    
    uint64 tail = poll_tail.fetch_add(1, std::memory_order_release);
    if (tail == poll_head.load(std::memory_order_acquire))
    {
        eventfd_write(io_ctx.efd, 1);
    }
}

void TcpServer::Send(List<ConnectionHandle>* conn_handles, ResponseBase* resp)
{
    uint8* buf;
    while (!io_ctx.send_buf_pool.Acquire(buf))
    {
        std::this_thread::yield();
    }

    // TODO: If non-thread safe container okey?
    ResponseArgs args = { conn_handles, buf, resp->Pack(buf) };
    response_allocator->Release(resp);

    while (!response_args.Enqueue(args))
    {
        std::this_thread::yield();
    }

    uint64 tail = poll_tail.fetch_add(1, std::memory_order_release);
    if (tail == poll_head.load(std::memory_order_acquire))
    {
        eventfd_write(io_ctx.efd, 1);
    }
}

void TcpServer::ProcessIOEvent(IOEventArgs* args, int32 res, uint32 flags)
{
    if (args->conn != nullptr && args->conn->version != args->conn_ver)
    {
        if ((flags & IORING_CQE_F_BUFFER) != 0)
        {
            io_ctx.ReturnBuffer(flags >> IORING_CQE_BUFFER_SHIFT);
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            event_args_pool.Release(args);
        }
        return;
    }

    switch (args->op)
    {
        case SocketOp::Poll: { OnPoll(args, res, flags); break; }
        case SocketOp::Accept: { OnAccept(args, res, flags); break; }
        case SocketOp::Receive: { OnReceive(args, res, flags); break; }
        case SocketOp::Send: { OnSend(args, res, flags); break; }
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

    RecvContext& recv_ctx = conn->recv_ctx;
    recv_ctx.packet_bytes = 0;
    recv_ctx.waiting_bytes = sizeof(packet_size);
    recv_ctx.received_bytes = 0;

    SendContext& send_ctx = conn->send_ctx;
    send_ctx.pending_bytes = 0;
    send_ctx.processed_bytes = 0;
    uint8* buf = send_ctx.buffer;
    while (buf != nullptr)
    {
        auto& send_meta = BufferMetadata::Get(buf, BUF_META_OFFSET).send;
        if (--send_meta.conn_count == 0)
        {
            while (!io_ctx.send_buf_pool.Release(buf)) { }
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

    conntions.Release(conn);
    sock_events.Enqueue({ ServerSocketEvent::Type::Disconnect, ConnectionHandle(conn) });
}

inline void TcpServer::PollInternal(IOEventArgs* args)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_poll_multishot(sqe, io_ctx.efd, POLLIN);
}

inline void TcpServer::AcceptInternal(IOEventArgs* args)
{
    static sockaddr_storage acpt_addr;
    static socklen_t acpt_addr_len = sizeof(sockaddr_storage);

    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_multishot_accept(sqe, sock_fd, (sockaddr*)&acpt_addr, &acpt_addr_len, 0);
}

inline void TcpServer::ReceiveInternal(IOEventArgs* args)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    sqe->buf_group = IO_RECV_BUF_GROUP;
    io_uring_prep_recv_multishot(sqe, args->conn->sock_fd, nullptr, 0, 0);
}

inline void TcpServer::SendInternal(IOEventArgs* args, const void* buf, int32 len)
{
    // TODO: When packet exceeds threshold, use io_uring_prep_send_zc?
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_send(sqe, args->conn->sock_fd, buf, len, 0);
}

void TcpServer::OnPoll(IOEventArgs* args, int32 res, uint32 flags)
{
    uint64 tail = poll_tail.load(std::memory_order_acquire);
    ResponseArgs resp_args;
    while (response_args.Dequeue(resp_args)) // TODO: Can batch pop.
    {
        if (resp_args.buffer == nullptr)
        {
            for (ConnectionHandle& conn_handle : *(resp_args.conn_handles))
            {
                CloseInternal(conn_handle);
            }
        }
        else
        {
            auto& send_meta = BufferMetadata::Get(resp_args.buffer, BUF_META_OFFSET).send;
            send_meta.conn_count = resp_args.conn_handles->size();
            send_meta.size = resp_args.length;
            for (ConnectionHandle& conn_handle : *(resp_args.conn_handles))
            {
                if (conn_handle.version != conn_handle.conn->version)
                {
                    send_meta.conn_count--;
                    continue;
                }

                SendContext& ctx = conn_handle.conn->send_ctx;
                if (ctx.pending_bytes > 0)
                {
                    // PERF: Merge packets if size less than threshold.
                    ctx.pending_responses.push(resp_args.buffer);
                }
                else
                {
                    ctx.buffer = resp_args.buffer;
                    ctx.pending_bytes = send_meta.size;
                    ctx.processed_bytes = 0;

                    IOEventArgs* send_args = event_args_pool.Acquire();
                    send_args->op = SocketOp::Send;
                    send_args->sock_ver = version;
                    send_args->sock_id = id;
                    send_args->conn_ver = conn_handle.conn->version;
                    send_args->conn = conn_handle;
                    SendInternal(send_args, ctx.buffer, send_meta.size);
                }
            }

            if (send_meta.conn_count <= 0)
            {
                while (!io_ctx.send_buf_pool.Release(resp_args.buffer)) { }
            }
        }
    }

    poll_head.store(tail, std::memory_order_release);
    if (tail != poll_tail.load(std::memory_order_acquire))
    {
        OnPoll(args, res, flags);
        return;
    }

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        if (res < 0)
        {
            Logging::Error("[TcpSocket] Failed to poll. Error: %s", strerror(-res));
            event_args_pool.Release(args);
        }
        else
        {
            PollInternal(args);
        }
    }
}

void TcpServer::OnAccept(IOEventArgs* args, int32 res, uint32 flags)
{
    static const std::unordered_set<int> fatal_errors =
    {
        EPERM, EBADF, EFAULT, EINVAL, ENOTSOCK, EOPNOTSUPP,
        EAFNOSUPPORT, EADDRINUSE, EADDRNOTAVAIL,
        ENETDOWN, ESHUTDOWN
    };

    if (res >= 0)
    {
        Connection* conn = conntions.Acquire();
        conn->sock_fd = res;
        sock_events.Enqueue({ ServerSocketEvent::Type::Connect, ConnectionHandle(conn) });

        IOEventArgs* args = event_args_pool.Acquire();
        args->op = SocketOp::Receive;
        args->sock_ver = version;
        args->sock_id = id;
        args->conn_ver = conn->version;
        args->conn = conn;
        ReceiveInternal(args);
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
            event_args_pool.Release(args);
            return;
        }

        Logging::Warning("[TcpSocket] Failed to accept connection. Error: %s", strerror(-res));
        std::this_thread::yield();
        AcceptInternal(args);
    }
}

void TcpServer::OnReceive(IOEventArgs* args, int32 res, uint32 flags)
{
    if (res <= 0)
    {
        Logging::Info("[TcpSocket] Connection closed. Socket: %d, Error: %s", args->conn->sock_fd, strerror(-res));
        CloseInternal(args->conn);

        if ((flags & IORING_CQE_F_BUFFER) != 0)
        {
            io_ctx.ReturnBuffer(flags >> IORING_CQE_BUFFER_SHIFT);
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            event_args_pool.Release(args);
        }
    }
    else
    {
        if ((flags & IORING_CQE_F_BUFFER) != 0)
        {
            int32 bid = flags >> IORING_CQE_BUFFER_SHIFT;
            uint8* buf = io_ctx.recv_buf_pool[bid];

            Logging::Info("[TcpSocket] Received %d bytes.", res);
            packet_size received_bytes = res;
            packet_size processed_bytes = 0;
            RecvContext& ctx = args->conn->recv_ctx;
            if (ctx.received_bytes > 0)
            {
                if (ctx.waiting_bytes > received_bytes)
                {
                    std::memcpy(ctx.buffer + ctx.received_bytes, buf, received_bytes);
                    ctx.waiting_bytes -= received_bytes;
                    ctx.received_bytes += received_bytes;

                    io_ctx.ReturnBuffer(bid);
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
                    RequestBase* req = request_allocator->Acquire(ctx.buffer);
                    req->conn_handle = ConnectionHandle(args->conn);
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
                    RequestBase* req = request_allocator->Acquire(buf + processed_bytes);
                    req->conn_handle = ConnectionHandle(args->conn);
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

            io_ctx.ReturnBuffer(bid);
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            ReceiveInternal(args);
        }
    }
}

void TcpServer::OnSend(IOEventArgs* args, int32 res, uint32 flags)
{
    if (res < 0)
    {
        Logging::Info("[TcpSocket] Connection closed while sending. Op: %d, Error: %s)", args->conn->sock_fd, strerror(-res));
        CloseInternal(args->conn); // PERF: Check if error is fatal.

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            event_args_pool.Release(args);
        }
        return;
    }

    SendContext& ctx = args->conn->send_ctx;
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

        auto& send_meta = BufferMetadata::Get(ctx.buffer, BUF_META_OFFSET).send;
        if (--send_meta.conn_count <= 0)
        {
            while (!io_ctx.send_buf_pool.Release(ctx.buffer)) { }
        }

        if (ctx.pending_responses.empty())
        {
            ctx.buffer = nullptr;
            ctx.pending_bytes = 0;
            event_args_pool.Release(args);
        }
        else
        {
            ctx.buffer = ctx.pending_responses.front();
            ctx.pending_bytes = BufferMetadata::Get(ctx.buffer, BUF_META_OFFSET).send.size;
            ctx.processed_bytes = 0;
            ctx.pending_responses.pop();
            SendInternal(args, ctx.buffer, ctx.pending_bytes);
        }
    }
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen