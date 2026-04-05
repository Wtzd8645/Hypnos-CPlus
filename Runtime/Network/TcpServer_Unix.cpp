#include "Hypnos/Network/NetworkDefs.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <liburing.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

namespace {

constexpr size_t MIN_SHARD_QUEUE_CAPACITY = 64;
constexpr size_t EVENT_ARGS_SLACK = 8;

size_t ResolveShardCapacity(int32 max_conns, size_t shard_count, size_t shard_index) noexcept
{
    const size_t total = max_conns > 0 ? static_cast<size_t>(max_conns) : 1;
    const size_t base = std::max<size_t>(1, total / std::max<size_t>(1, shard_count));
    const size_t remainder = total % std::max<size_t>(1, shard_count);
    return base + (shard_index < remainder ? 1 : 0);
}

size_t ResolveArgsCapacity(size_t connection_capacity) noexcept
{
    return connection_capacity * 2 + 1 + EVENT_ARGS_SLACK;
}

} // namespace

TcpServer::ShardState::ShardState(TcpServer& server, uint16 rid, IOContext& io_ctx, size_t capacity) :
server(server),
rid(rid),
io_ctx(io_ctx),
framing_buffer_pool(MAX_BUFFER_SIZE),
connections(capacity),
conn_events(std::max<size_t>(MIN_SHARD_QUEUE_CAPACITY, capacity)),
requests(std::max<size_t>(MIN_SHARD_QUEUE_CAPACITY, capacity)),
command_queue(std::max<size_t>(MIN_SHARD_QUEUE_CAPACITY, capacity))
{
}

TcpServer::TcpServer(uint32 id, const ServerConfig& cfg, const List<IOContext*>& ctxs) : ServerBase(id, cfg.max_conns),
cfg(cfg),
io_contexts(ctxs)
{
    request_allocator = cfg.request_allocator;
    response_allocator = cfg.response_allocator;

    shards.reserve(io_contexts.size());
    for (size_t shard_index = 0; shard_index < io_contexts.size(); ++shard_index)
    {
        shards.push_back(new ShardState(*this, static_cast<uint16>(shard_index), *io_contexts[shard_index], ResolveShardCapacity(cfg.max_conns, io_contexts.size(), shard_index)));
    }
}

TcpServer::~TcpServer()
{
    Stop();
    for (ShardState* shard : shards)
    {
        delete shard;
    }

    if (request_allocator != nullptr)
    {
        delete request_allocator;
    }
}

Status<void> TcpServer::Start()
{
    bool already_running = false;
    for (const ShardState* shard : shards)
    {
        already_running |= shard->listen_fd > INVALID_FD;
    }

    if (already_running)
    {
        return Status<void>::Error(ErrorCode::InvalidHandle, "[TcpSocket] Socket is already running.");
    }

    for (ShardState* shard : shards)
    {
        Status<void> status = shard->framing_buffer_pool.Allocate(shard->connections.Capacity());
        if (status.IsFailed())
        {
            return status;
        }

        status = shard->args_pool.Allocate(ResolveArgsCapacity(shard->connections.Capacity()));
        if (status.IsFailed())
        {
            return status;
        }

        shard->listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
        if (shard->listen_fd < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to create socket.");
        }

        int opt = 1;
        if (setsockopt(shard->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to set SO_REUSEADDR.");
        }

        if (setsockopt(shard->listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to set SO_REUSEPORT.");
        }

        opt = 0;
        if (setsockopt(shard->listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to set IPv6 dual-stack mode.");
        }

        sockaddr_in6 sock_addr = { AF_INET6, htons(cfg.bind_port), 0, cfg.bind_ip, 0 };
        if (bind(shard->listen_fd, reinterpret_cast<sockaddr*>(&sock_addr), sizeof(sock_addr)) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to bind socket.");
        }

        if (listen(shard->listen_fd, cfg.listen_backlog) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpSocket] Failed to listen on socket.");
        }

        CompletionArgs* args = shard->args_pool.Acquire();
        if (args == nullptr)
        {
            return Status<void>::Error(ErrorCode::OutOfMemory, "[TcpSocket] Failed to acquire accept event args.");
        }

        BindAcceptArgs(*shard, *args);
        AcceptInternal(*shard, args);
    }

    Logging::Info("[TcpSocket] Listening on %zu reactor shards.", shards.size());
    return Status<void>::Success();
}

Status<void> TcpServer::Stop()
{
    bool stopped_any = false;
    for (ShardState* shard : shards)
    {
        if (shard->listen_fd > INVALID_FD)
        {
            shutdown(shard->listen_fd, SHUT_RDWR);
            close(shard->listen_fd);
            shard->listen_fd = INVALID_FD;
            stopped_any = true;
        }

        List<Connection*> active_connections;
        active_connections.reserve(shard->connections.Count());
        for (auto& conn : shard->connections)
        {
            if (conn.sock_fd > INVALID_FD)
            {
                active_connections.push_back(&conn);
            }
        }

        for (Connection* conn : active_connections)
        {
            CloseInternal(*shard, conn);
            stopped_any = true;
        }
    }

    if (!stopped_any)
    {
        return Status<void>::Error(ErrorCode::InvalidHandle, "[TcpSocket] Socket is not running.");
    }

    return Status<void>::Success();
}

void TcpServer::Dispatch()
{
    for (ShardState* shard : shards)
    {
        ConnectionEvent evt;
        while (shard->conn_events.Dequeue(evt))
        {
            conn_event_handlers[static_cast<size_t>(evt.type)](evt);
        }

        IMessage* req = nullptr;
        while (shard->requests.Dequeue(req))
        {
            if (request_dispatcher != nullptr)
            {
                request_dispatcher->Dispatch(*req);
            }

            if (message_codec != nullptr)
            {
                message_codec->Release(req);
            }
            else if (request_allocator != nullptr)
            {
                request_allocator->Release(req);
            }
        }
    }
}

void TcpServer::OnReactorWake(uint16 rid)
{
    DrainCommands(GetShard(rid));
}

Status<void> TcpServer::Close(const List<ConnectionHandle>& conn_handles)
{
    List<List<ConnectionHandle>> grouped(shards.size());
    for (const ConnectionHandle& conn_handle : conn_handles)
    {
        if (!conn_handle.IsValid() || conn_handle.rid >= shards.size())
        {
            continue;
        }

        grouped[conn_handle.rid].push_back(conn_handle);
    }

    bool enqueued = false;
    for (size_t rid = 0; rid < grouped.size(); ++rid)
    {
        if (grouped[rid].empty())
        {
            continue;
        }

        ShardState& shard = *shards[rid];
        if (shard.command_queue.RemainingCapacity() == 0)
        {
            return Status<void>::Error(ErrorCode::Busy, "[TcpSocket] Close queue is full.");
        }
    }

    for (size_t rid = 0; rid < grouped.size(); ++rid)
    {
        if (grouped[rid].empty())
        {
            continue;
        }

        ResponseArgs args;
        args.type = ReactorCommandType::Close;
        args.conn_handles = grouped[rid];

        ShardState& shard = *shards[rid];
        if (!shard.command_queue.Enqueue(args))
        {
            return Status<void>::Error(ErrorCode::Busy, "[TcpSocket] Failed to enqueue close command.");
        }

        shard.io_ctx.Notify();
        enqueued = true;
    }

    return enqueued ? Status<void>::Success() : Status<void>::Error(ErrorCode::InvalidArgument, "[TcpSocket] No valid connections to close.");
}

Status<void> TcpServer::Broadcast(IMessage* resp)
{
    (void)resp;
    return Status<void>::Error(ErrorCode::Unsupported, "[TcpSocket] Broadcast is not implemented yet.");
}

Status<void> TcpServer::Send(const List<ConnectionHandle>& conn_handles, IMessage* resp)
{
    if (resp == nullptr)
    {
        return Status<void>::Error(ErrorCode::InvalidArgument, "[TcpSocket] Response cannot be null.");
    }

    if (response_allocator == nullptr)
    {
        return Status<void>::Error(ErrorCode::NotReady, "[TcpSocket] Response allocator is not configured.");
    }

    List<List<ConnectionHandle>> grouped(shards.size());
    for (const ConnectionHandle& conn_handle : conn_handles)
    {
        if (!conn_handle.IsValid() || conn_handle.rid >= shards.size())
        {
            continue;
        }

        grouped[conn_handle.rid].push_back(conn_handle);
    }

    List<ResponseArgs> pending_commands;
    pending_commands.reserve(shards.size());

    for (size_t rid = 0; rid < grouped.size(); ++rid)
    {
        if (grouped[rid].empty())
        {
            continue;
        }

        ShardState& shard = *shards[rid];
        if (shard.command_queue.RemainingCapacity() == 0)
        {
            for (ResponseArgs& pending : pending_commands)
            {
                ShardState& pending_shard = GetShard(pending.conn_handles.front().rid);
                if (!pending_shard.io_ctx.send_buf_pool.Release(pending.buffer))
                {
                    Logging::Error("[TcpSocket] Failed to roll back send buffer after queue pressure.");
                }
            }

            return Status<void>::Error(ErrorCode::Busy, "[TcpSocket] Send queue is full.");
        }

        byte* buf = nullptr;
        if (!shard.io_ctx.send_buf_pool.Acquire(buf))
        {
            for (ResponseArgs& pending : pending_commands)
            {
                ShardState& pending_shard = GetShard(pending.conn_handles.front().rid);
                if (!pending_shard.io_ctx.send_buf_pool.Release(pending.buffer))
                {
                    Logging::Error("[TcpSocket] Failed to roll back send buffer after pool exhaustion.");
                }
            }

            return Status<void>::Error(ErrorCode::Busy, "[TcpSocket] Send buffer pool is exhausted.");
        }

        ResponseArgs args;
        args.type = ReactorCommandType::Send;
        args.conn_handles = grouped[rid];
        args.buffer = buf;
        args.length = resp->Pack(buf);
        pending_commands.push_back(std::move(args));
    }

    if (pending_commands.empty())
    {
        return Status<void>::Error(ErrorCode::InvalidArgument, "[TcpSocket] No valid target connections.");
    }

    for (ResponseArgs& pending : pending_commands)
    {
        ShardState& shard = GetShard(pending.conn_handles.front().rid);
        if (!shard.command_queue.Enqueue(pending))
        {
            Logging::Error("[TcpSocket] Failed to enqueue send command after preflight.");
            return Status<void>::Error(ErrorCode::Busy, "[TcpSocket] Failed to enqueue send command.");
        }

        shard.io_ctx.Notify();
    }

    if (response_allocator != nullptr)
    {
        response_allocator->Release(resp);
    }

    return Status<void>::Success();
}

TcpServer::ShardState& TcpServer::GetShard(uint16 rid) const
{
    assert(rid < shards.size() && "[TcpSocket] Reactor index out of bounds.");
    return *shards[rid];
}

Connection* TcpServer::ResolveHandle(ShardState& shard, const ConnectionHandle& handle) const
{
    if (!handle.IsValid() || handle.rid != shard.rid)
    {
        return nullptr;
    }

    Connection* conn = shard.connections.TryGet(handle.slot);
    if (conn == nullptr || conn->version != handle.version || conn->sock_fd <= INVALID_FD)
    {
        return nullptr;
    }

    return conn;
}

void TcpServer::BindAcceptArgs(ShardState& shard, CompletionArgs& args) const
{
    args.complete = &TcpServer::OnAcceptCqe;
    args.owner = &shard;
    args.rid = shard.rid;
    args.ep_ver = version;
    args.gid = cfg.io_buf_gid;
    args.conn_ver = 0;
    args.conn = nullptr;
}

void TcpServer::BindReceiveArgs(ShardState& shard, CompletionArgs& args, Connection& conn) const
{
    args.complete = &TcpServer::OnReceiveCqe;
    args.owner = &shard;
    args.rid = shard.rid;
    args.ep_ver = version;
    args.gid = cfg.io_buf_gid;
    args.conn_ver = conn.version;
    args.conn = &conn;
}

void TcpServer::BindSendArgs(ShardState& shard, CompletionArgs& args, Connection& conn) const
{
    args.complete = &TcpServer::OnSendCqe;
    args.owner = &shard;
    args.rid = shard.rid;
    args.ep_ver = version;
    args.gid = cfg.io_buf_gid;
    args.conn_ver = conn.version;
    args.conn = &conn;
}

bool TcpServer::IsStale(const CompletionArgs& args) const noexcept
{
    return args.ep_ver != version || (args.conn != nullptr && args.conn_ver != args.conn->version);
}

void TcpServer::ReleaseReceiveArgs(ShardState& shard, CompletionArgs* args, uint32 flags)
{
    if ((flags & IORING_CQE_F_BUFFER) != 0)
    {
        shard.io_ctx.ReleaseBuffer(args->gid, flags >> IORING_CQE_BUFFER_SHIFT);
    }

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        shard.args_pool.Release(args);
    }
}

void TcpServer::ReleaseSendArgs(ShardState& shard, CompletionArgs* args, uint32 flags)
{
    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        shard.args_pool.Release(args);
    }
}

void TcpServer::OnAcceptCqe(int32 res, uint32 flags, CompletionArgs* args)
{
    ShardState& shard = *static_cast<ShardState*>(args->owner);
    CompletionArgs& accept_args = *args;
    if (shard.server.IsStale(accept_args))
    {
        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            shard.args_pool.Release(&accept_args);
        }

        return;
    }

    shard.server.OnAccept(shard, res, flags, accept_args);
}

void TcpServer::OnReceiveCqe(int32 res, uint32 flags, CompletionArgs* args)
{
    ShardState& shard = *static_cast<ShardState*>(args->owner);
    if (shard.server.IsStale(*args))
    {
        shard.server.ReleaseReceiveArgs(shard, args, flags);
        return;
    }

    shard.server.OnReceive(shard, res, flags, *args);
}

void TcpServer::OnSendCqe(int32 res, uint32 flags, CompletionArgs* args)
{
    ShardState& shard = *static_cast<ShardState*>(args->owner);
    if (shard.server.IsStale(*args))
    {
        shard.server.ReleaseSendArgs(shard, args, flags);
        return;
    }

    shard.server.OnSend(shard, res, flags, *args);
}

void TcpServer::CloseInternal(ShardState& shard, Connection* conn)
{
    if (conn == nullptr || conn->sock_fd <= INVALID_FD)
    {
        return;
    }

    ConnectionHandle conn_handle(*conn);
    shutdown(conn->sock_fd, SHUT_RDWR);
    close(conn->sock_fd);
    conn->sock_fd = INVALID_FD;
    conn->version++;

    SendContext& send_ctx = conn->send_ctx;
    send_ctx.pending_bytes = 0;
    send_ctx.processed_bytes = 0;
    byte* buf = send_ctx.buffer;
    send_ctx.buffer = nullptr;
    while (buf != nullptr)
    {
        auto& send_meta = BufferMetadata::Get(buf, IOContext::BUF_META_OFFSET).send;
        if (--send_meta.conn_count == 0 && !shard.io_ctx.send_buf_pool.Release(buf))
        {
            Logging::Error("[TcpSocket] Failed to release send buffer during close.");
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
    recv_ctx.received_bytes = 0;
    recv_ctx.header = { };
    if (recv_ctx.buffer != nullptr)
    {
        shard.framing_buffer_pool.Release(recv_ctx.buffer);
        recv_ctx.buffer = nullptr;
    }

    shard.connections.Release(conn);
    if (!shard.conn_events.Enqueue({ ConnectionEvent::Type::Disconnected, conn_handle }))
    {
        Logging::Warning("[TcpSocket] Dropped disconnect event on reactor %d.", shard.rid);
    }
}

void TcpServer::DrainCommands(ShardState& shard)
{
    ResponseArgs resp_args;
    while (shard.command_queue.Dequeue(resp_args))
    {
        if (resp_args.type == ReactorCommandType::Close)
        {
            for (const ConnectionHandle& conn_handle : resp_args.conn_handles)
            {
                Connection* conn = ResolveHandle(shard, conn_handle);
                if (conn != nullptr)
                {
                    CloseInternal(shard, conn);
                }
            }

            continue;
        }

        auto& send_meta = BufferMetadata::Get(resp_args.buffer, IOContext::BUF_META_OFFSET).send;
        send_meta.conn_count = 0;
        send_meta.size = resp_args.length;
        for (const ConnectionHandle& conn_handle : resp_args.conn_handles)
        {
            Connection* conn = ResolveHandle(shard, conn_handle);
            if (conn == nullptr)
            {
                continue;
            }

            ++send_meta.conn_count;
            SendContext& ctx = conn->send_ctx;
            if (ctx.pending_bytes > 0)
            {
                ctx.pending_responses.push(resp_args.buffer);
                continue;
            }

            CompletionArgs* args = shard.args_pool.Acquire();
            if (args == nullptr)
            {
                --send_meta.conn_count;
                CloseInternal(shard, conn);
                continue;
            }

            ctx.buffer = resp_args.buffer;
            ctx.pending_bytes = send_meta.size;
            ctx.processed_bytes = 0;

            BindSendArgs(shard, *args, *conn);
            SendInternal(shard, args, ctx.buffer, send_meta.size);
        }

        if (send_meta.conn_count <= 0 && !shard.io_ctx.send_buf_pool.Release(resp_args.buffer))
        {
            Logging::Error("[TcpSocket] Failed to release unused send buffer.");
        }
    }
}

void TcpServer::AcceptInternal(ShardState& shard, CompletionArgs* args)
{
    shard.accept_addr_len = sizeof(sockaddr_storage);

    io_uring_sqe* sqe = shard.io_ctx.AcquireSqe();
    if (sqe == nullptr)
    {
        Logging::Error("[TcpSocket] Failed to acquire SQE for accept.");
        return;
    }

    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_multishot_accept(sqe, shard.listen_fd, reinterpret_cast<sockaddr*>(&shard.accept_addr), &shard.accept_addr_len, 0);
}

void TcpServer::ReceiveInternal(ShardState& shard, CompletionArgs* args)
{
    io_uring_sqe* sqe = shard.io_ctx.AcquireSqe();
    if (sqe == nullptr)
    {
        Logging::Error("[TcpSocket] Failed to acquire SQE for receive.");
        CloseInternal(shard, args->conn);
        shard.args_pool.Release(args);
        return;
    }

    io_uring_sqe_set_data(sqe, args);
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    sqe->buf_group = args->gid;
    io_uring_prep_recv(sqe, args->conn->sock_fd, nullptr, 0, 0);
}

void TcpServer::SendInternal(ShardState& shard, CompletionArgs* args, const void* buf, int32 len)
{
    io_uring_sqe* sqe = shard.io_ctx.AcquireSqe();
    if (sqe == nullptr)
    {
        Logging::Error("[TcpSocket] Failed to acquire SQE for send.");
        CloseInternal(shard, args->conn);
        shard.args_pool.Release(args);
        return;
    }

    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_send(sqe, args->conn->sock_fd, buf, len, 0);
}

bool TcpServer::HandlePacket(ShardState& shard, const TransportHeader& header, byte* src)
{
    const packet_size payload_len = header.length - TRANSPORT_HEADER_SIZE;
    const byte* decoded_buf = src;
    packet_size decoded_len = payload_len;

    if (header.codec != PACKET_CODEC_NONE)
    {
        if (packet_codec == nullptr)
        {
            return false;
        }

        decoded_buf = packet_codec->Decode(header.codec, src, payload_len, decoded_len);
        if (decoded_buf == nullptr)
        {
            return false;
        }
    }

    if (message_codec == nullptr)
    {
        return false;
    }

    IMessage* request = message_codec->Decode(decoded_buf, decoded_len);
    if (request == nullptr)
    {
        return false;
    }

    if (!shard.requests.Enqueue(request))
    {
        message_codec->Release(request);
        return false;
    }

    return true;
}

void TcpServer::OnAccept(ShardState& shard, int32 res, uint32 flags, CompletionArgs& args)
{
    static const std::unordered_set<int> fatal_errors =
    {
        EPERM, EBADF, EFAULT, EINVAL, ENOTSOCK, EOPNOTSUPP,
        EAFNOSUPPORT, EADDRINUSE, EADDRNOTAVAIL,
        ENETDOWN, ESHUTDOWN
    };

    if (res >= 0)
    {
        Connection* conn = shard.connections.Acquire();
        if (conn == nullptr)
        {
            Logging::Warning("[TcpSocket] Connection pool is full on reactor %d.", shard.rid);
            shutdown(res, SHUT_RDWR);
            close(res);
        }
        else
        {
            conn->sock_fd = res;
            conn->rid = shard.rid;
            conn->slot = static_cast<uint16>(shard.connections.IndexOf(conn));
            conn->recv_ctx.buffer = shard.framing_buffer_pool.Acquire();

            int no_delay = 1;
            setsockopt(conn->sock_fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));

            if (conn->recv_ctx.buffer == nullptr)
            {
                CloseInternal(shard, conn);
            }
            else
            {
                if (!shard.conn_events.Enqueue({ ConnectionEvent::Type::Connected, ConnectionHandle(*conn) }))
                {
                    Logging::Warning("[TcpSocket] Dropped connect event on reactor %d.", shard.rid);
                }

                CompletionArgs* recv_args = shard.args_pool.Acquire();
                if (recv_args == nullptr)
                {
                    CloseInternal(shard, conn);
                }
                else
                {
                    BindReceiveArgs(shard, *recv_args, *conn);
                    ReceiveInternal(shard, recv_args);
                }
            }
        }
    }

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        if (res >= 0)
        {
            AcceptInternal(shard, &args);
            return;
        }

        if (fatal_errors.contains(-res))
        {
            Logging::Error("[TcpSocket] Failed to accept connection. Error: %s", strerror(-res));
            shard.args_pool.Release(&args);
            return;
        }

        Logging::Warning("[TcpSocket] Failed to accept connection. Error: %s", strerror(-res));
        AcceptInternal(shard, &args);
    }
}

void TcpServer::OnReceive(ShardState& shard, int32 res, uint32 flags, CompletionArgs& args)
{
    if (res <= 0)
    {
        Logging::Info("[TcpSocket] Connection closed. Socket: %d, Error: %s", args.conn->sock_fd, strerror(-res));
        CloseInternal(shard, args.conn);
        ReleaseReceiveArgs(shard, &args, flags);
        return;
    }

    if ((flags & IORING_CQE_F_BUFFER) == 0)
    {
        Logging::Error("[TcpSocket] No buffer selected for received data.");
        CloseInternal(shard, args.conn);
        ReleaseReceiveArgs(shard, &args, flags);
        return;
    }

    byte* recv_buf = shard.io_ctx.AcquireBuffer(args.gid, flags >> IORING_CQE_BUFFER_SHIFT);
    if (recv_buf == nullptr)
    {
        Logging::Error("[TcpSocket] Invalid receive buffer group or buffer id.");
        CloseInternal(shard, args.conn);
        ReleaseReceiveArgs(shard, &args, flags);
        return;
    }

    RecvContext& ctx = args.conn->recv_ctx;
    packet_size pending_bytes = static_cast<packet_size>(res);
    packet_size offset = 0;
    while (pending_bytes > 0)
    {
        if (ctx.received_bytes < TRANSPORT_HEADER_SIZE)
        {
            const packet_size header_needed = TRANSPORT_HEADER_SIZE - ctx.received_bytes;
            const packet_size copied = std::min(header_needed, pending_bytes);
            std::memcpy(ctx.buffer + ctx.received_bytes, recv_buf + offset, copied);
            ctx.received_bytes += copied;
            pending_bytes -= copied;
            offset += copied;

            if (ctx.received_bytes < TRANSPORT_HEADER_SIZE)
            {
                break;
            }

            std::memcpy(&ctx.header, ctx.buffer, TRANSPORT_HEADER_SIZE);
            if (ctx.header.length < TRANSPORT_HEADER_SIZE || ctx.header.length > MAX_PACKET_SIZE)
            {
                CloseInternal(shard, args.conn);
                ReleaseReceiveArgs(shard, &args, flags);
                return;
            }
        }

        const packet_size frame_needed = ctx.header.length - ctx.received_bytes;
        const packet_size copied = std::min(frame_needed, pending_bytes);
        std::memcpy(ctx.buffer + ctx.received_bytes, recv_buf + offset, copied);
        ctx.received_bytes += copied;
        pending_bytes -= copied;
        offset += copied;

        if (ctx.received_bytes < ctx.header.length)
        {
            break;
        }

        if (!HandlePacket(shard, ctx.header, ctx.buffer + TRANSPORT_HEADER_SIZE))
        {
            CloseInternal(shard, args.conn);
            ReleaseReceiveArgs(shard, &args, flags);
            return;
        }

        ctx.received_bytes = 0;
        ctx.header = { };
    }

    shard.io_ctx.ReleaseBuffer(args.gid, flags >> IORING_CQE_BUFFER_SHIFT);
    ReceiveInternal(shard, &args);
}

void TcpServer::OnSend(ShardState& shard, int32 res, uint32 flags, CompletionArgs& args)
{
    if (res < 0)
    {
        Logging::Info("[TcpSocket] Connection closed while sending. Socket: %d, Error: %s", args.conn->sock_fd, strerror(-res));
        CloseInternal(shard, args.conn);
        ReleaseSendArgs(shard, &args, flags);
        return;
    }

    SendContext& ctx = args.conn->send_ctx;
    ctx.pending_bytes -= static_cast<packet_size>(res);
    ctx.processed_bytes += static_cast<packet_size>(res);
    if (ctx.pending_bytes > 0)
    {
        SendInternal(shard, &args, ctx.buffer + ctx.processed_bytes, ctx.pending_bytes);
        return;
    }

    auto& send_meta = BufferMetadata::Get(ctx.buffer, IOContext::BUF_META_OFFSET).send;
    if (--send_meta.conn_count <= 0 && !shard.io_ctx.send_buf_pool.Release(ctx.buffer))
    {
        Logging::Error("[TcpSocket] Failed to release send buffer after completion.");
    }

    if (ctx.pending_responses.empty())
    {
        ctx.buffer = nullptr;
        ctx.pending_bytes = 0;
        ctx.processed_bytes = 0;
        shard.args_pool.Release(&args);
        return;
    }

    ctx.buffer = ctx.pending_responses.front();
    ctx.pending_responses.pop();
    ctx.pending_bytes = BufferMetadata::Get(ctx.buffer, IOContext::BUF_META_OFFSET).send.size;
    ctx.processed_bytes = 0;
    SendInternal(shard, &args, ctx.buffer, ctx.pending_bytes);
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
