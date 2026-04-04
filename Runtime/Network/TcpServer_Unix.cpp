#include "Hypnos/Network/NetworkDefs.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <cstring>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <thread>
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

TcpServer::TcpServer(uint32 id, const ServerConfig& cfg, IOContext& ctx) : ServerBase(id, cfg.max_conns),
cfg(cfg),
io_ctx(ctx),
framing_buffer_pool(MAX_BUFFER_SIZE),
requests(cfg.max_conns),
response_args(cfg.max_conns)
{
    request_allocator = cfg.request_allocator;
    response_allocator = cfg.response_allocator;
}

TcpServer::~TcpServer()
{
    Stop();
    if (request_allocator != nullptr)
    {
        delete request_allocator;
    }
}

Status<void> TcpServer::Start()
{
    if (sock_fd > INVALID_FD)
    {
        return Status<void>::Error(ErrorCode::InvalidHandle, "[TcpSocket] Socket is already running.");
    }

    Status<void> status = framing_buffer_pool.Allocate(max_conns);
    if (status.IsFailed())
    {
        return status;
    }

    status = event_args_pool.Allocate(max_conns / 2);
    if (status.IsFailed())
    {
        return status;
    }

    sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to create socket.");
    }

    int opt = 0;
    if (setsockopt(sock_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
    {
        return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to set socket options.");
    }

    //setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)); // TODO: Make configurable.
    //setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)); // TODO: Make configurable.

    sockaddr_in6 sock_addr = { AF_INET6, htons(cfg.bind_port), 0, cfg.bind_ip, 0 };
    if (bind(sock_fd, (sockaddr*)&sock_addr, sizeof(sock_addr)) < 0)
    {
        return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to bind socket.");
    }

    if (listen(sock_fd, 256) < 0) // TODO: Make configurable.
    {
        return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to listen on socket.");
    }

    Logging::Info("[TcpSocket] Listening on socket.");
    IOEventArgs* args = event_args_pool.Acquire();
    args->op = SocketOp::Poll;
    args->ep_ver = version;
    args->ep_id = static_cast<uint8>(id);
    args->gid = cfg.io_buf_gid;
    args->conn_ver = 0;
    args->conn = nullptr;
    PollInternal(args);

    args = event_args_pool.Acquire();
    args->op = SocketOp::Accept;
    args->ep_ver = version;
    args->ep_id = static_cast<uint8>(id);
    args->gid = cfg.io_buf_gid;
    args->conn_ver = 0;
    args->conn = nullptr;
    AcceptInternal(args);
    return Status<void>::Success();
}

void TcpServer::Broadcast(IMessage* resp)
{
    // TODO: Current response queue stores connection-handle list by pointer, so a safe zero-copy broadcast path is not wired yet.
    Logging::Warning("[TcpSocket] Broadcast is not implemented.");
    if (response_allocator != nullptr)
    {
        response_allocator->Release(resp);
    }
}

Status<void> TcpServer::Stop()
{
    if (sock_fd <= INVALID_FD)
    {
        return Status<void>::Error(ErrorCode::InvalidHandle, "[TcpSocket] Socket is not running.");
    }

    Logging::Info("[TcpSocket] Closing all active connections. %d", id);
    // TODO: It need to send poll to close connection and wait for completion.
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
    return Status<void>::Success();
}

void TcpServer::Dispatch()
{
    ConnectionEvent evt;
    while (conn_events.Dequeue(evt))
    {
        conn_event_handlers[static_cast<size_t>(evt.type)](evt);
    }

    IMessage* req = nullptr;
    while (requests.Dequeue(req)) // PERF: Can batch pop.
    {
        request_dispatcher->Dispatch(*req);
        if (message_codec != nullptr)
        {
            message_codec->Release(req);
        }
        else
        {
            request_allocator->Release(req);
        }
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

    if (poll_count.fetch_add(1, std::memory_order_release))
    {
        eventfd_write(io_ctx.event_fd, 1);
    }
}

void TcpServer::Send(List<ConnectionHandle>* conn_handles, IMessage* resp)
{
    byte* buf;
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

    if (poll_count.fetch_add(1, std::memory_order_release))
    {
        eventfd_write(io_ctx.event_fd, 1);
    }
}

void TcpServer::Process(const IOEvent& evt)
{
    IOEventArgs* args = evt.args;
    if (args->ep_ver != version || (args->conn != nullptr && args->conn_ver != args->conn->version))
    {
        ReleaseEventArgs(args, evt.flags);
        return;
    }

    switch (args->op)
    {
        case SocketOp::Accept: { OnAccept(evt); break; }
        case SocketOp::Receive: { OnReceive(evt); break; }
        case SocketOp::Poll: { OnPoll(evt); break; }
        case SocketOp::Send: { OnSend(evt); break; }
        default: { Logging::Error("[TcpSocket] Unknown operation type."); break; }
    }
}

void TcpServer::ReleaseEventArgs(IOEventArgs* args, uint32 flags)
{
    if ((flags & IORING_CQE_F_BUFFER) != 0)
    {
        io_ctx.ReleaseBuffer(args->gid, flags >> IORING_CQE_BUFFER_SHIFT);
    }

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        event_args_pool.Release(args);
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

    SendContext& send_ctx = conn->send_ctx;
    send_ctx.pending_bytes = 0;
    send_ctx.processed_bytes = 0;
    byte* buf = send_ctx.buffer;
    while (buf != nullptr)
    {
        auto& send_meta = BufferMetadata::Get(buf, IOContext::BUF_META_OFFSET).send;
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

    RecvContext& recv_ctx = conn->recv_ctx;
    if (recv_ctx.buffer != nullptr)
    {
        framing_buffer_pool.Release(recv_ctx.buffer);
        recv_ctx.buffer = nullptr;
    }

    conntions.Release(conn);
    conn_events.Enqueue({ ConnectionEvent::Type::Disconnected, ConnectionHandle(conn) });
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
    sqe->buf_group = args->gid;
    io_uring_prep_recv_multishot(sqe, args->conn->sock_fd, nullptr, 0, 0);
}

inline void TcpServer::PollInternal(IOEventArgs* args)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_poll_multishot(sqe, io_ctx.event_fd, POLLIN);
}

inline void TcpServer::SendInternal(IOEventArgs* args, const void* buf, int32 len)
{
    // TODO: When packet exceeds threshold, use io_uring_prep_send_zc?
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ctx.ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_send(sqe, args->conn->sock_fd, buf, len, 0);
    // io_uring_prep_send_zc(sqe, args->conn->sock_fd, packet_buf, packet_len, 0, 0);
}

bool TcpServer::HandlePacket(const TransportHeader& header, byte* src)
{
    packet_size decoded_len = 0;
    const byte* decoded_buf = packet_codec->Decode(header.codec, src, header.length, decoded_len);
    if (decoded_buf == nullptr)
    {
        return false;
    }

    IMessage* request = message_codec->Decode(decoded_buf, decoded_len);
    if (request == nullptr)
    {
        return false;
    }

    while (!requests.Enqueue(request))
    {
        std::this_thread::yield();
    }
    return true;
}

void TcpServer::OnAccept(const IOEvent& evt)
{
    static const std::unordered_set<int> fatal_errors =
    {
        EPERM, EBADF, EFAULT, EINVAL, ENOTSOCK, EOPNOTSUPP,
        EAFNOSUPPORT, EADDRINUSE, EADDRNOTAVAIL,
        ENETDOWN, ESHUTDOWN
    };

    if (evt.res >= 0)
    {
        Connection* conn = conntions.Acquire();
        conn->sock_fd = evt.res;
        conn->recv_ctx.buffer = framing_buffer_pool.Acquire(); // TODO: Handle buffer acquire failure.
        conn_events.Enqueue({ ConnectionEvent::Type::Connected, ConnectionHandle(conn) });

        IOEventArgs* args = event_args_pool.Acquire();
        args->op = SocketOp::Receive;
        args->ep_ver = version;
        args->ep_id = id;
        args->gid = cfg.io_buf_gid;
        args->conn_ver = conn->version;
        args->conn = conn;
        ReceiveInternal(args);
    }

    if ((evt.flags & IORING_CQE_F_MORE) == 0)
    {
        if (evt.res >= 0)
        {
            AcceptInternal(evt.args);
            return;
        }

        if (fatal_errors.contains(-evt.res))
        {
            Logging::Error("[TcpSocket] Failed to accept connection. Error: %s", strerror(-evt.res));
            event_args_pool.Release(evt.args);
            return;
        }

        Logging::Warning("[TcpSocket] Failed to accept connection. Error: %s", strerror(-evt.res));
        std::this_thread::yield();
        AcceptInternal(evt.args);
    }
}

// TODO: Change to use IOU_PBUF_RING_INC and check for IORING_CQE_F_BUF_MORE flag.
void TcpServer::OnReceive(const IOEvent& evt)
{
    // Check for connection closure or errors
    if (evt.res <= 0)
    {
        Logging::Info("[TcpSocket] Connection closed. Socket: %d, Error: %s", evt.args->conn->sock_fd, strerror(-evt.res));
        CloseInternal(evt.args->conn);
        ReleaseEventArgs(evt.args, evt.flags);
        return;
    }

    // Check if a buffer was successfully selected
    if ((evt.flags & IORING_CQE_F_BUFFER) == 0)
    {
        Logging::Error("[TcpSocket] No buffer selected for received data.");
        CloseInternal(evt.args->conn);
        ReleaseEventArgs(evt.args, evt.flags);
        return;
    }

    // Get the receive buffer
    Logging::Info("[TcpSocket] Received %d bytes.", evt.res);
    uint16 gid = evt.args->gid;
    uint16 bid = evt.flags >> IORING_CQE_BUFFER_SHIFT;
    byte* buf = io_ctx.AcquireBuffer(gid, bid);
    if (buf == nullptr)
    {
        Logging::Error("[TcpSocket] Invalid receive buffer group or buffer id.");
        CloseInternal(evt.args->conn);
        ReleaseEventArgs(evt.args, evt.flags);
        return;
    }

    RecvContext& ctx = evt.args->conn->recv_ctx;
    packet_size pending_bytes = evt.res;
    packet_size offset = 0;
    while (pending_bytes > 0)
    {
        packet_size needed_bytes;
        if (ctx.header.length == 0)
        {
            if (pending_bytes + ctx.received_bytes < TRANSPORT_HEADER_SIZE)
            {
                std::memcpy(ctx.buffer + ctx.received_bytes, buf + offset, pending_bytes);
                ctx.received_bytes += pending_bytes;
                break;
            }

            if (ctx.received_bytes == 0)
            {
                needed_bytes = TRANSPORT_HEADER_SIZE;
                std::memcpy(&ctx.header, buf + offset, TRANSPORT_HEADER_SIZE);
            }
            else
            {
                needed_bytes = TRANSPORT_HEADER_SIZE - ctx.received_bytes;
                std::memcpy(ctx.buffer + ctx.received_bytes, buf + offset, needed_bytes);
                ctx.received_bytes = 0;
                std::memcpy(&ctx.header, ctx.buffer, TRANSPORT_HEADER_SIZE);
            }

            pending_bytes -= needed_bytes;
            offset += needed_bytes;

            if (ctx.header.length > MAX_PACKET_SIZE)
            {
                CloseInternal(evt.args->conn);
                ReleaseEventArgs(evt.args, evt.flags);
                return;
            }
        }

        if (pending_bytes + ctx.received_bytes < ctx.header.length)
        {
            std::memcpy(ctx.buffer + ctx.received_bytes, buf + offset, pending_bytes);
            ctx.received_bytes += pending_bytes;
            break;
        }

        byte* packet_buf;
        if (ctx.received_bytes == 0)
        {
            needed_bytes = ctx.header.length;
            packet_buf = buf + offset;
        }
        else
        {
            needed_bytes = ctx.header.length - ctx.received_bytes;
            std::memcpy(ctx.buffer + ctx.received_bytes, buf + offset, needed_bytes);
            ctx.received_bytes = 0;
            packet_buf = ctx.buffer;
        }

        if (!HandlePacket(ctx.header, packet_buf))
        {
            CloseInternal(evt.args->conn);
            ReleaseEventArgs(evt.args, evt.flags);
            return;
        }

        ctx.header = { };
        pending_bytes -= needed_bytes;
        offset += needed_bytes;
    }

    io_ctx.ReleaseBuffer(gid, bid);
    if ((evt.flags & IORING_CQE_F_MORE) == 0)
    {
        ReceiveInternal(evt.args);
    }
}

void TcpServer::OnPoll(const IOEvent& evt)
{
    uint64 count = 0;
    ResponseArgs resp_args;
    while (response_args.Dequeue(resp_args)) // TODO: Can batch pop.
    {
        count++;
        if (resp_args.buffer == nullptr)
        {
            for (ConnectionHandle& conn_handle : *(resp_args.conn_handles))
            {
                CloseInternal(conn_handle);
            }
        }
        else
        {
            auto& send_meta = BufferMetadata::Get(resp_args.buffer, IOContext::BUF_META_OFFSET).send;
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

                    IOEventArgs* args = event_args_pool.Acquire();
                    args->op = SocketOp::Send;
                    args->ep_ver = version;
                    args->ep_id = static_cast<uint8>(id);
                    args->gid = cfg.io_buf_gid;
                    args->conn_ver = conn_handle.conn->version;
                    args->conn = conn_handle;
                    SendInternal(args, ctx.buffer, send_meta.size);
                }
            }

            if (send_meta.conn_count <= 0)
            {
                while (!io_ctx.send_buf_pool.Release(resp_args.buffer)) { }
            }
        }
    }

    if (poll_count.fetch_sub(count, std::memory_order_release) > count)
    {
        OnPoll(evt);
        return;
    }

    if ((evt.flags & IORING_CQE_F_MORE) == 0)
    {
        if (evt.res < 0)
        {
            Logging::Error("[TcpSocket] Failed to poll. Error: %s", strerror(-evt.res));
            event_args_pool.Release(evt.args);
        }
        else
        {
            PollInternal(evt.args);
        }
    }
}

void TcpServer::OnSend(const IOEvent& evt)
{
    if (evt.res < 0)
    {
        Logging::Info("[TcpSocket] Connection closed while sending. Op: %d, Error: %s)", evt.args->conn->sock_fd, strerror(-evt.res));
        CloseInternal(evt.args->conn); // PERF: Check if error is fatal.
        ReleaseEventArgs(evt.args, evt.flags);
        return;
    }

    SendContext& ctx = evt.args->conn->send_ctx;
    if ((evt.flags & IORING_CQE_F_NOTIF) == 0)
    {
        Logging::Info("[TcpSocket] Sent %d bytes.", evt.res);
        ctx.pending_bytes -= evt.res;
        ctx.processed_bytes += evt.res;
    }

    if ((evt.flags & IORING_CQE_F_MORE) == 0)
    {
        if (ctx.pending_bytes > 0)
        {
            SendInternal(evt.args, ctx.buffer + ctx.processed_bytes, ctx.pending_bytes);
            return;
        }

        auto& send_meta = BufferMetadata::Get(ctx.buffer, IOContext::BUF_META_OFFSET).send;
        if (--send_meta.conn_count <= 0)
        {
            while (!io_ctx.send_buf_pool.Release(ctx.buffer)) { }
        }

        if (ctx.pending_responses.empty())
        {
            ctx.buffer = nullptr;
            ctx.pending_bytes = 0;
            event_args_pool.Release(evt.args);
        }
        else
        {
            ctx.buffer = ctx.pending_responses.front();
            ctx.pending_bytes = BufferMetadata::Get(ctx.buffer, IOContext::BUF_META_OFFSET).send.size;
            ctx.processed_bytes = 0;
            ctx.pending_responses.pop();
            SendInternal(evt.args, ctx.buffer, ctx.pending_bytes);
        }
    }
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
