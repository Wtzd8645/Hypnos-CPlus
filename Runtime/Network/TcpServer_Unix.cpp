#include "Hypnos/Network/NetworkDefs.hpp"
#include "TcpServer.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <liburing.h>
#include <limits>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <new>
#include <sys/socket.h>
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
    const size_t normalized_shard_count = std::max<size_t>(1, shard_count);
    const size_t base = std::max<size_t>(1, total / normalized_shard_count);
    const size_t remainder = total % normalized_shard_count;
    return base + (shard_index < remainder ? 1 : 0);
}

BufferMetadata::Send& GetSendMetadata(byte* buffer) noexcept
{
    assert(buffer != nullptr && "[TcpServer] Send buffer must not be null.");
    return BufferMetadata::GetSend(buffer);
}

BufferMetadata::Send& InitializeSendMetadata(byte* buffer) noexcept
{
    return *new (&BufferMetadata::GetSend(buffer)) BufferMetadata::Send();
}

size_t ResolveIngressQueueCapacity(size_t connection_capacity, uint32 max_pending_buffers) noexcept
{
    return std::max<size_t>(MIN_SHARD_QUEUE_CAPACITY, connection_capacity * std::max<uint32>(1, max_pending_buffers) + EVENT_ARGS_SLACK);
}

} // namespace

ServerShard::ServerShard(NetworkShard& shard, size_t queue_capacity_hint, uint32 max_pending_buffers) :
    shard(shard),
    egress_connection_events(std::max<size_t>(MIN_SHARD_QUEUE_CAPACITY, queue_capacity_hint)),
    egress_requests(std::max<size_t>(MIN_SHARD_QUEUE_CAPACITY, queue_capacity_hint)),
    ingress_commands(ResolveIngressQueueCapacity(queue_capacity_hint, max_pending_buffers))
{
}

TcpServer::TcpServer(uint32 id, const ServerConfig& cfg, uint16 total_shard_count, const List<NetworkShard*>& bound_shards) :
    Server(id, cfg.max_conns),
    EndpointRuntime(static_cast<uint8>(id), EndpointRuntime::Type::Server),
    cfg(cfg)
{
    request_allocator = cfg.request_allocator;

    server_shards.reserve(bound_shards.size());
    server_shard_lookup.assign(total_shard_count, nullptr);
    for (size_t bound_shard_index = 0; bound_shard_index < bound_shards.size(); ++bound_shard_index)
    {
        NetworkShard& shard = *bound_shards[bound_shard_index];
        ServerShard* server_shard = new ServerShard(shard,
                                                    ResolveShardCapacity(cfg.max_conns, bound_shards.size(), bound_shard_index),
                                                    cfg.max_pending_send_buffers_per_connection);
        server_shards.push_back(server_shard);
        server_shard_lookup[shard.id] = server_shard;
    }

    shard_target_counts.assign(total_shard_count, 0);
    shard_target_offsets.assign(total_shard_count, 0);
    touched_shards.reserve(server_shards.size());
    admission_records.reserve(std::max<size_t>(1, static_cast<size_t>(std::max<int32>(1, cfg.max_conns)) * cfg.max_pending_send_buffers_per_connection));
    prepared_commands.reserve(server_shards.size());
}

TcpServer::~TcpServer()
{
    Stop();
    for (ServerShard* server_shard : server_shards)
    {
        delete server_shard;
    }

    if (request_allocator != nullptr)
    {
        delete request_allocator;
    }
}

Status<void> TcpServer::Start()
{
    AssertOwnerThread("[TcpServer] Start must be called from the endpoint owner thread.");

    bool already_running = false;
    for (const ServerShard* server_shard : server_shards)
    {
        already_running |= server_shard->listen_fd > INVALID_FD;
    }

    if (already_running)
    {
        return Status<void>::Error(ErrorCode::InvalidHandle, "[TcpServer] Socket is already running.");
    }

    DrainReturnedBuffers();

    for (ServerShard* server_shard : server_shards)
    {
        server_shard->listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
        if (server_shard->listen_fd < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpServer] Failed to create socket.");
        }

        int opt = 1;
        if (setsockopt(server_shard->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpServer] Failed to set SO_REUSEADDR.");
        }

        if (setsockopt(server_shard->listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpServer] Failed to set SO_REUSEPORT.");
        }

        opt = 0;
        if (setsockopt(server_shard->listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpServer] Failed to set IPv6 dual-stack mode.");
        }

        sockaddr_in6 sock_addr = { AF_INET6, htons(cfg.bind_port), 0, cfg.bind_ip, 0 };
        if (bind(server_shard->listen_fd, reinterpret_cast<sockaddr*>(&sock_addr), sizeof(sock_addr)) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpServer] Failed to bind socket.");
        }

        if (listen(server_shard->listen_fd, cfg.listen_backlog) < 0)
        {
            return Status<void>::Error(ErrorCode::IOError, "[TcpServer] Failed to listen on socket.");
        }

        CompletionArgs* args = server_shard->shard.args_pool.Acquire();
        if (args == nullptr)
        {
            return Status<void>::Error(ErrorCode::OutOfMemory, "[TcpServer] Failed to acquire accept event args.");
        }

        AcceptInternal(*server_shard, args);
    }

    Logging::Info("[TcpServer] Listening on %zu network shards.", server_shards.size());
    return Status<void>::Success();
}

Status<void> TcpServer::Stop()
{
    AssertOwnerThread("[TcpServer] Stop must be called from the endpoint owner thread.");

    DrainReturnedBuffers();

    bool stopped_any = false;
    for (ServerShard* server_shard : server_shards)
    {
        if (server_shard->listen_fd > INVALID_FD)
        {
            shutdown(server_shard->listen_fd, SHUT_RDWR);
            close(server_shard->listen_fd);
            server_shard->listen_fd = INVALID_FD;
            stopped_any = true;
        }

        NetworkShard& shard = server_shard->shard;
        for (size_t slot_index = 0; slot_index < shard.connection_capacity; ++slot_index)
        {
            Connection& conn = shard.connections[slot_index];
            if (conn.endpoint_id == static_cast<uint16>(Server::id) &&
                (conn.state.load(std::memory_order_acquire) == ConnectionState::Connected || conn.sock_fd > INVALID_FD))
            {
                CloseInternal(*server_shard, conn);
                stopped_any = true;
            }
        }

        ServerShardCommand command;
        while (server_shard->ingress_commands.Dequeue(command))
        {
            if (command.type == ServerShardCommandType::SendBatch)
            {
                for (uint32 index = 0; index < command.conn_handle_count; ++index)
                {
                    const ConnectionHandle& conn_handle = command.conn_handles[index];
                    const uint16 slot = conn_handle.slot;
                    if (slot < shard.connection_capacity)
                    {
                        Connection& conn = shard.connections[slot];
                        if (conn.endpoint_id == static_cast<uint16>(Server::id))
                        {
                            ReleaseQueuedSend(conn);
                            FinalizeClosingConnection(*server_shard, conn);
                        }

                        ReleaseBufferReference(server_shard->shard, command.buffer);
                    }
                    else
                    {
                        ReleaseBufferReference(server_shard->shard, command.buffer);
                    }
                }

                delete[] command.conn_handles;
                stopped_any = true;
                continue;
            }

            delete[] command.conn_handles;
            stopped_any = true;
        }

        for (size_t slot_index = 0; slot_index < shard.connection_capacity; ++slot_index)
        {
            Connection& conn = shard.connections[slot_index];
            if (conn.endpoint_id == static_cast<uint16>(Server::id))
            {
                FinalizeClosingConnection(*server_shard, conn);
            }
        }
    }

    DrainReturnedBuffers();

    if (!stopped_any)
    {
        return Status<void>::Error(ErrorCode::InvalidHandle, "[TcpServer] Socket is not running.");
    }

    return Status<void>::Success();
}

void TcpServer::Dispatch()
{
    AssertOwnerThread("[TcpServer] Dispatch must be called from the endpoint owner thread.");

    DrainReturnedBuffers();

    for (ServerShard* server_shard : server_shards)
    {
        ConnectionEvent evt;
        while (server_shard->egress_connection_events.Dequeue(evt))
        {
            conn_event_handlers[ToIndex(evt.type)](evt);
        }

        IMessage* req = nullptr;
        while (server_shard->egress_requests.Dequeue(req))
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

void TcpServer::OnShardWake(uint16 shard_id)
{
    DrainIngressCommands(GetServerShard(shard_id));
}

Status<void> TcpServer::Close(std::span<const ConnectionHandle> conn_handles)
{
    AssertOwnerThread("[TcpServer] Close must be called from the endpoint owner thread.");

    std::fill(shard_target_counts.begin(), shard_target_counts.end(), 0);
    touched_shards.clear();
    prepared_commands.clear();

    for (const ConnectionHandle& conn_handle : conn_handles)
    {
        if (!conn_handle.IsValid() ||
            conn_handle.endpoint_id != static_cast<uint16>(Server::id) ||
            conn_handle.shard_id >= server_shard_lookup.size() ||
            server_shard_lookup[conn_handle.shard_id] == nullptr)
        {
            return Status<void>::Error(ErrorCode::InvalidHandle, "[TcpServer] Close target handle is invalid.");
        }

        const uint16 shard_id = conn_handle.shard_id;
        if (shard_target_counts[shard_id]++ == 0)
        {
            touched_shards.push_back(shard_id);
        }
    }

    if (touched_shards.empty())
    {
        return Status<void>::Error(ErrorCode::InvalidArgument, "[TcpServer] No valid connections to close.");
    }

    for (uint16 shard_id : touched_shards)
    {
        if (GetServerShard(shard_id).ingress_commands.RemainingCapacity() < 1)
        {
            for (uint16 touched_shard_id : touched_shards)
            {
                shard_target_counts[touched_shard_id] = 0;
            }
            touched_shards.clear();
            return Status<void>::Error(ErrorCode::Busy, "[TcpServer] Close queue is full.");
        }
    }

    List<uint32> prepared_indices(server_shard_lookup.size(), std::numeric_limits<uint32>::max());
    for (uint16 shard_id : touched_shards)
    {
        PreparedCommand prepared { };
        prepared.server_shard = &GetServerShard(shard_id);
        prepared.conn_handle_count = shard_target_counts[shard_id];
        prepared.conn_handles = new (std::nothrow) ConnectionHandle[prepared.conn_handle_count];
        if (prepared.conn_handles == nullptr)
        {
            for (PreparedCommand& rollback_prepared : prepared_commands)
            {
                delete[] rollback_prepared.conn_handles;
            }

            prepared_commands.clear();
            for (uint16 touched_shard_id : touched_shards)
            {
                shard_target_counts[touched_shard_id] = 0;
            }
            touched_shards.clear();
            return Status<void>::Error(ErrorCode::OutOfMemory, "[TcpServer] Failed to allocate close batch handles.");
        }

        prepared_indices[shard_id] = static_cast<uint32>(prepared_commands.size());
        prepared_commands.push_back(prepared);
        shard_target_offsets[shard_id] = 0;
    }

    for (const ConnectionHandle& conn_handle : conn_handles)
    {
        const uint16 shard_id = conn_handle.shard_id;
        PreparedCommand& prepared = prepared_commands[prepared_indices[shard_id]];
        prepared.conn_handles[shard_target_offsets[shard_id]++] = conn_handle;
    }

    for (PreparedCommand& prepared : prepared_commands)
    {
        ServerShardCommand command;
        command.type = ServerShardCommandType::CloseBatch;
        command.conn_handles = prepared.conn_handles;
        command.conn_handle_count = prepared.conn_handle_count;

        const bool enqueued = prepared.server_shard->ingress_commands.Enqueue(command);
        assert(enqueued && "[TcpServer] Close batch enqueue should succeed after preflight.");
        (void)enqueued;

        prepared.conn_handles = nullptr;
    }

    for (uint16 shard_id : touched_shards)
    {
        GetServerShard(shard_id).shard.Notify();
        shard_target_counts[shard_id] = 0;
        shard_target_offsets[shard_id] = 0;
    }

    prepared_commands.clear();
    touched_shards.clear();
    return Status<void>::Success();
}

Status<void> TcpServer::Broadcast(const IMessage& message)
{
    (void)message;
    return Status<void>::Error(ErrorCode::Unsupported, "[TcpServer] Broadcast is not implemented yet.");
}

Status<void> TcpServer::Send(std::span<const ConnectionHandle> conn_handles, const IMessage& message)
{
    AssertOwnerThread("[TcpServer] Send must be called from the endpoint owner thread.");

    if (conn_handles.empty())
    {
        return Status<void>::Error(ErrorCode::InvalidArgument, "[TcpServer] No target connections to send.");
    }

    if (conn_handles.size() > admission_records.capacity())
    {
        return Status<void>::Error(ErrorCode::InvalidArgument, "[TcpServer] Target count exceeds the preallocated send admission capacity.");
    }

    DrainReturnedBuffers();

    std::fill(shard_target_counts.begin(), shard_target_counts.end(), 0);
    touched_shards.clear();
    admission_records.clear();
    prepared_commands.clear();

    auto rollback_send = [this]()
    {
        for (AdmissionRecord& record : admission_records)
        {
            ReleaseQueuedSend(*record.conn);
        }

        for (PreparedCommand& prepared : prepared_commands)
        {
            if (prepared.buffer != nullptr)
            {
                prepared.server_shard->shard.send_buffer_pool.Release(prepared.buffer);
            }

            delete[] prepared.conn_handles;
        }

        for (uint16 shard_id : touched_shards)
        {
            shard_target_counts[shard_id] = 0;
            shard_target_offsets[shard_id] = 0;
        }

        admission_records.clear();
        prepared_commands.clear();
        touched_shards.clear();
    };

    for (const ConnectionHandle& conn_handle : conn_handles)
    {
        if (!conn_handle.IsValid() ||
            conn_handle.endpoint_id != static_cast<uint16>(Server::id) ||
            conn_handle.shard_id >= server_shard_lookup.size() ||
            server_shard_lookup[conn_handle.shard_id] == nullptr)
        {
            rollback_send();
            return Status<void>::Error(ErrorCode::InvalidHandle, "[TcpServer] Send target handle is invalid.");
        }

        const uint16 shard_id = conn_handle.shard_id;
        ServerShard& server_shard = GetServerShard(shard_id);
        Connection* conn = ResolveHandle(server_shard, conn_handle);
        if (conn == nullptr)
        {
            rollback_send();
            return Status<void>::Error(ErrorCode::InvalidHandle, "[TcpServer] Send target connection is stale or disconnected.");
        }

        if (!TryReserveQueuedSend(*conn))
        {
            rollback_send();
            return Status<void>::Error(ErrorCode::Busy, "[TcpServer] Connection send backlog is full.");
        }

        if (server_shard.shard.send_buffer_pool.Capacity() == 0)
        {
            rollback_send();
            return Status<void>::Error(ErrorCode::NotReady, "[TcpServer] Send buffer pool is not initialized.");
        }

        if (shard_target_counts[shard_id]++ == 0)
        {
            touched_shards.push_back(shard_id);
        }

        admission_records.push_back({ conn, shard_id });
    }

    for (uint16 shard_id : touched_shards)
    {
        if (GetServerShard(shard_id).ingress_commands.RemainingCapacity() < 1)
        {
            rollback_send();
            return Status<void>::Error(ErrorCode::Busy, "[TcpServer] Send queue is full.");
        }
    }

    List<uint32> prepared_indices(server_shard_lookup.size(), std::numeric_limits<uint32>::max());
    for (uint16 shard_id : touched_shards)
    {
        ServerShard& server_shard = GetServerShard(shard_id);

        PreparedCommand prepared { };
        prepared.server_shard = &server_shard;
        prepared.conn_handle_count = shard_target_counts[shard_id];
        prepared.conn_handles = new (std::nothrow) ConnectionHandle[prepared.conn_handle_count];
        if (prepared.conn_handles == nullptr)
        {
            rollback_send();
            return Status<void>::Error(ErrorCode::OutOfMemory, "[TcpServer] Failed to allocate send batch handles.");
        }

        prepared.buffer = server_shard.shard.send_buffer_pool.Acquire();
        if (prepared.buffer == nullptr)
        {
            delete[] prepared.conn_handles;
            rollback_send();
            return Status<void>::Error(ErrorCode::Busy, "[TcpServer] Send buffer pool is exhausted.");
        }

        BufferMetadata::Send& send_meta = InitializeSendMetadata(prepared.buffer);
        send_meta.ref_count.store(0, std::memory_order_release);
        send_meta.size = 0;
        send_meta.reserved = 0;

        Status<PacketSize> packed = message.Pack(prepared.buffer, static_cast<PacketSize>(MAX_PACKET_SIZE));
        if (packed.IsFailed())
        {
            server_shard.shard.send_buffer_pool.Release(prepared.buffer);
            delete[] prepared.conn_handles;
            rollback_send();
            return Status<void>::Error(static_cast<ErrorCode>(packed.ErrorCode()), packed.Message());
        }

        if (packed.Value() > MAX_PACKET_SIZE)
        {
            server_shard.shard.send_buffer_pool.Release(prepared.buffer);
            delete[] prepared.conn_handles;
            rollback_send();
            return Status<void>::Error(ErrorCode::InvalidFormat, "[TcpServer] Encoded packet exceeded the fixed MTU payload capacity.");
        }

        send_meta.size = packed.Value();
        send_meta.ref_count.store(prepared.conn_handle_count, std::memory_order_release);

        prepared_indices[shard_id] = static_cast<uint32>(prepared_commands.size());
        prepared_commands.push_back(prepared);
        shard_target_offsets[shard_id] = 0;
    }

    for (const ConnectionHandle& conn_handle : conn_handles)
    {
        const uint16 shard_id = conn_handle.shard_id;
        PreparedCommand& prepared = prepared_commands[prepared_indices[shard_id]];
        prepared.conn_handles[shard_target_offsets[shard_id]++] = conn_handle;
    }

    for (PreparedCommand& prepared : prepared_commands)
    {
        ServerShardCommand command;
        command.type = ServerShardCommandType::SendBatch;
        command.conn_handles = prepared.conn_handles;
        command.conn_handle_count = prepared.conn_handle_count;
        command.buffer = prepared.buffer;

        const bool enqueued = prepared.server_shard->ingress_commands.Enqueue(command);
        assert(enqueued && "[TcpServer] Send batch enqueue should succeed after preflight.");
        (void)enqueued;

        prepared.conn_handles = nullptr;
        prepared.buffer = nullptr;
    }

    for (uint16 shard_id : touched_shards)
    {
        GetServerShard(shard_id).shard.Notify();
        shard_target_counts[shard_id] = 0;
        shard_target_offsets[shard_id] = 0;
    }

    prepared_commands.clear();
    admission_records.clear();
    touched_shards.clear();
    return Status<void>::Success();
}

ServerShard& TcpServer::GetServerShard(uint16 shard_id) const
{
    assert(shard_id < server_shard_lookup.size() && "[TcpServer] Shard index out of bounds.");
    ServerShard* server_shard = server_shard_lookup[shard_id];
    assert(server_shard != nullptr && "[TcpServer] Endpoint is not bound to the target shard.");
    return *server_shard;
}

Connection* TcpServer::ResolveHandle(ServerShard& server_shard, const ConnectionHandle& handle) const
{
    if (!handle.IsValid() ||
        handle.endpoint_id != static_cast<uint16>(Server::id) ||
        handle.shard_id != server_shard.shard.id ||
        handle.slot >= server_shard.shard.connection_capacity)
    {
        return nullptr;
    }

    Connection& conn = server_shard.shard.connections[handle.slot];
    if (conn.endpoint_id != handle.endpoint_id ||
        conn.shard_id != handle.shard_id ||
        conn.slot != handle.slot)
    {
        return nullptr;
    }

    if (conn.state.load(std::memory_order_acquire) != ConnectionState::Connected)
    {
        return nullptr;
    }

    if (conn.generation.load(std::memory_order_acquire) != handle.generation)
    {
        return nullptr;
    }

    if (conn.sock_fd <= INVALID_FD)
    {
        return nullptr;
    }

    return &conn;
}

void TcpServer::DrainReturnedBuffers()
{
    for (ServerShard* server_shard : server_shards)
    {
        server_shard->shard.DrainReturnedSendBuffers();
    }
}

bool TcpServer::TryReserveQueuedSend(Connection& conn) const
{
    uint32 queued_send_count = conn.queued_send_count.load(std::memory_order_acquire);
    while (true)
    {
        if (queued_send_count >= cfg.max_pending_send_buffers_per_connection)
        {
            return false;
        }

        if (conn.queued_send_count.compare_exchange_weak(queued_send_count, queued_send_count + 1, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return true;
        }
    }
}

void TcpServer::ReleaseQueuedSend(Connection& conn) noexcept
{
    const uint32 queued_send_count = conn.queued_send_count.fetch_sub(1, std::memory_order_acq_rel);
    assert(queued_send_count > 0 && "[TcpServer] Queued send count underflow.");
    (void)queued_send_count;
}

void TcpServer::ReleaseBufferReference(NetworkShard& shard, byte* buffer) noexcept
{
    if (shard.send_buffer_pool.Capacity() == 0 || buffer == nullptr)
    {
        return;
    }

    BufferMetadata::Send& send_meta = GetSendMetadata(buffer);
    const uint32 ref_count = send_meta.ref_count.fetch_sub(1, std::memory_order_acq_rel);
    assert(ref_count > 0 && "[TcpServer] Send buffer reference count underflow.");
    if (ref_count == 1)
    {
        send_meta.size = 0;
        if (!shard.returned_send_buffers.Enqueue(buffer))
        {
            Logging::Error("[TcpServer] Failed to recycle send buffer on shard %d.", shard.id);
        }
    }
}

void TcpServer::FinalizeClosingConnection(ServerShard& server_shard, Connection& conn)
{
    if (conn.state.load(std::memory_order_acquire) != ConnectionState::Closing)
    {
        return;
    }

    if (conn.sock_fd > INVALID_FD)
    {
        return;
    }

    if (conn.queued_send_count.load(std::memory_order_acquire) != 0)
    {
        return;
    }

    server_shard.shard.ReleaseConnection(conn);
}

void TcpServer::CloseInternal(ServerShard& server_shard, Connection& conn)
{
    ConnectionState expected = ConnectionState::Connected;
    if (!conn.state.compare_exchange_strong(expected, ConnectionState::Closing, std::memory_order_acq_rel, std::memory_order_acquire))
    {
        if (expected == ConnectionState::Closing)
        {
            FinalizeClosingConnection(server_shard, conn);
        }

        return;
    }

    ConnectionHandle conn_handle = conn.CreateHandle();
    if (conn.sock_fd > INVALID_FD)
    {
        shutdown(conn.sock_fd, SHUT_RDWR);
        close(conn.sock_fd);
        conn.sock_fd = INVALID_FD;
    }

    SendContext& send_ctx = conn.send_ctx;
    if (send_ctx.active_buffer != nullptr)
    {
        ReleaseQueuedSend(conn);
        ReleaseBufferReference(server_shard.shard, send_ctx.active_buffer);
        send_ctx.active_buffer = nullptr;
    }

    send_ctx.pending_bytes = 0;
    send_ctx.processed_bytes = 0;

    byte* queued_buffer = nullptr;
    while (send_ctx.pending_buffers.Pop(queued_buffer))
    {
        ReleaseQueuedSend(conn);
        ReleaseBufferReference(server_shard.shard, queued_buffer);
    }
    send_ctx.pending_buffers.Reset();

    RecvContext& recv_ctx = conn.recv_ctx;
    recv_ctx.received_bytes = 0;
    recv_ctx.header = { };
    if (recv_ctx.buffer != nullptr)
    {
        server_shard.shard.framing_buffer_pool.Release(recv_ctx.buffer);
        recv_ctx.buffer = nullptr;
    }

    conn.generation.fetch_add(1, std::memory_order_acq_rel);
    if (!server_shard.egress_connection_events.Enqueue({ ConnectionEventType::Disconnected, conn_handle }))
    {
        Logging::Warning("[TcpServer] Dropped disconnect event on shard %d.", server_shard.shard.id);
    }

    FinalizeClosingConnection(server_shard, conn);
}

void TcpServer::DrainIngressCommands(ServerShard& server_shard)
{
    ServerShardCommand command;
    while (server_shard.ingress_commands.Dequeue(command))
    {
        if (command.type == ServerShardCommandType::CloseBatch)
        {
            ExecuteCloseBatch(server_shard, command.conn_handles, command.conn_handle_count);
            continue;
        }

        ExecuteSendBatch(server_shard, command.conn_handles, command.conn_handle_count, command.buffer);
    }
}

void TcpServer::ExecuteCloseBatch(ServerShard& server_shard, ConnectionHandle* conn_handles, uint32 conn_handle_count)
{
    for (uint32 index = 0; index < conn_handle_count; ++index)
    {
        Connection* conn = ResolveHandle(server_shard, conn_handles[index]);
        if (conn != nullptr)
        {
            CloseInternal(server_shard, *conn);
        }
    }

    delete[] conn_handles;
}

void TcpServer::ExecuteSendBatch(ServerShard& server_shard, ConnectionHandle* conn_handles, uint32 conn_handle_count, byte* buffer)
{
    for (uint32 index = 0; index < conn_handle_count; ++index)
    {
        const ConnectionHandle& conn_handle = conn_handles[index];
        Connection* conn = ResolveHandle(server_shard, conn_handle);
        if (conn == nullptr)
        {
            const uint16 slot = conn_handle.slot;
            if (slot < server_shard.shard.connection_capacity)
            {
                Connection& queued_conn = server_shard.shard.connections[slot];
                if (queued_conn.endpoint_id == static_cast<uint16>(Server::id))
                {
                    ReleaseQueuedSend(queued_conn);
                    FinalizeClosingConnection(server_shard, queued_conn);
                }

                ReleaseBufferReference(server_shard.shard, buffer);
            }
            else
            {
                ReleaseBufferReference(server_shard.shard, buffer);
            }

            continue;
        }

        SendContext& send_ctx = conn->send_ctx;
        if (send_ctx.active_buffer != nullptr)
        {
            if (!send_ctx.pending_buffers.Push(buffer))
            {
                Logging::Error("[TcpServer] Pending send buffer queue overflow on shard %d.", server_shard.shard.id);
                ReleaseQueuedSend(*conn);
                ReleaseBufferReference(server_shard.shard, buffer);
                CloseInternal(server_shard, *conn);
            }

            continue;
        }

        CompletionArgs* args = server_shard.shard.args_pool.Acquire();
        if (args == nullptr)
        {
            ReleaseQueuedSend(*conn);
            ReleaseBufferReference(server_shard.shard, buffer);
            CloseInternal(server_shard, *conn);
            continue;
        }

        send_ctx.active_buffer = buffer;
        send_ctx.pending_bytes = GetSendMetadata(buffer).size;
        send_ctx.processed_bytes = 0;

        args->data.send.conn = conn;
        SendInternal(server_shard, args, buffer, send_ctx.pending_bytes);
    }

    delete[] conn_handles;
}

void TcpServer::AcceptInternal(ServerShard& server_shard, CompletionArgs* args)
{
    args->complete = &TcpServer::OnAcceptCqe;
    args->owner = this;
    args->type = CompletionArgs::Type::Accept;
    args->data.accept.shard_id = server_shard.shard.id;

    server_shard.accept_addr_len = sizeof(sockaddr_storage);

    io_uring_sqe* sqe = server_shard.shard.AcquireSqe();
    if (sqe == nullptr)
    {
        Logging::Error("[TcpServer] Failed to acquire SQE for accept.");
        return;
    }

    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_multishot_accept(sqe, server_shard.listen_fd, reinterpret_cast<sockaddr*>(&server_shard.accept_addr), &server_shard.accept_addr_len, 0);
}

void TcpServer::ReceiveInternal(ServerShard& server_shard, CompletionArgs* args)
{
    Connection* conn = args->data.recv.conn;
    assert(conn != nullptr && "[TcpServer] Receive args must bind a connection.");

    args->complete = &TcpServer::OnReceiveCqe;
    args->owner = this;
    args->type = CompletionArgs::Type::Recv;
    args->data.recv.generation = conn->generation.load(std::memory_order_acquire);

    io_uring_sqe* sqe = server_shard.shard.AcquireSqe();
    if (sqe == nullptr)
    {
        Logging::Error("[TcpServer] Failed to acquire SQE for receive.");
        CloseInternal(server_shard, *conn);
        server_shard.shard.args_pool.Release(args);
        return;
    }

    io_uring_sqe_set_data(sqe, args);
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    sqe->buf_group = cfg.io_buf_gid;
    io_uring_prep_recv(sqe, conn->sock_fd, nullptr, 0, 0);
}

void TcpServer::SendInternal(ServerShard& server_shard, CompletionArgs* args, const void* buf, int32 len)
{
    Connection* conn = args->data.send.conn;
    assert(conn != nullptr && "[TcpServer] Send args must bind a connection.");

    args->complete = &TcpServer::OnSendCqe;
    args->owner = this;
    args->type = CompletionArgs::Type::Send;
    args->data.send.generation = conn->generation.load(std::memory_order_acquire);

    io_uring_sqe* sqe = server_shard.shard.AcquireSqe();
    if (sqe == nullptr)
    {
        Logging::Error("[TcpServer] Failed to acquire SQE for send.");
        CloseInternal(server_shard, *conn);
        server_shard.shard.args_pool.Release(args);
        return;
    }

    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_send(sqe, conn->sock_fd, buf, len, 0);
}

bool TcpServer::IsStale(const CompletionArgs& args) const noexcept
{
    if (args.type == CompletionArgs::Type::Accept)
    {
        return args.data.accept.shard_id >= server_shard_lookup.size() ||
               server_shard_lookup[args.data.accept.shard_id] == nullptr;
    }

    if (args.type == CompletionArgs::Type::Recv)
    {
        const Connection* conn = args.data.recv.conn;
        if (conn == nullptr || conn->endpoint_id != static_cast<uint16>(Server::id))
        {
            return true;
        }

        return args.data.recv.generation != conn->generation.load(std::memory_order_acquire);
    }

    if (args.type == CompletionArgs::Type::Send)
    {
        const Connection* conn = args.data.send.conn;
        if (conn == nullptr || conn->endpoint_id != static_cast<uint16>(Server::id))
        {
            return true;
        }

        return args.data.send.generation != conn->generation.load(std::memory_order_acquire);
    }

    return true;
}

void TcpServer::ReleaseReceiveArgs(ServerShard& server_shard, CompletionArgs* args, uint32 flags)
{
    if ((flags & IORING_CQE_F_BUFFER) != 0)
    {
        server_shard.shard.ReleaseBuffer(cfg.io_buf_gid, flags >> IORING_CQE_BUFFER_SHIFT);
    }

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        server_shard.shard.args_pool.Release(args);
    }
}

void TcpServer::ReleaseSendArgs(ServerShard& server_shard, CompletionArgs* args, uint32 flags)
{
    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        server_shard.shard.args_pool.Release(args);
    }
}

bool TcpServer::HandlePacket(ServerShard& server_shard, const TransportHeader& header, byte* src)
{
    const PacketSize payload_len = header.length - TRANSPORT_HEADER_SIZE;
    const byte* decoded_buf = src;
    PacketSize decoded_len = payload_len;

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

    if (!server_shard.egress_requests.Enqueue(request))
    {
        message_codec->Release(request);
        return false;
    }

    return true;
}

void TcpServer::OnAcceptCqe(int32 res, uint32 flags, CompletionArgs* args)
{
    TcpServer& server = *static_cast<TcpServer*>(args->owner);
    assert(args->type == CompletionArgs::Type::Accept && "[TcpServer] Unexpected completion args type for accept CQE.");
    ServerShard& server_shard = server.GetServerShard(args->data.accept.shard_id);
    CompletionArgs& accept_args = *args;
    if (server.IsStale(accept_args))
    {
        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            server_shard.shard.args_pool.Release(&accept_args);
        }

        return;
    }

    server.OnAccept(server_shard, res, flags, accept_args);
}

void TcpServer::OnReceiveCqe(int32 res, uint32 flags, CompletionArgs* args)
{
    TcpServer& server = *static_cast<TcpServer*>(args->owner);
    assert(args->type == CompletionArgs::Type::Recv && "[TcpServer] Unexpected completion args type for receive CQE.");

    if (server.IsStale(*args))
    {
        Connection* conn = args->data.recv.conn;
        assert(conn != nullptr && "[TcpServer] Receive completion args must bind a connection.");

        ServerShard& server_shard = server.GetServerShard(conn->shard_id);
        server.ReleaseReceiveArgs(server_shard, args, flags);
        return;
    }

    server.OnReceive(res, flags, *args);
}

void TcpServer::OnSendCqe(int32 res, uint32 flags, CompletionArgs* args)
{
    TcpServer& server = *static_cast<TcpServer*>(args->owner);
    assert(args->type == CompletionArgs::Type::Send && "[TcpServer] Unexpected completion args type for send CQE.");
    Connection* conn = args->data.send.conn;
    assert(conn != nullptr && "[TcpServer] Send completion args must bind a connection.");
    if (server.IsStale(*args))
    {
        ServerShard& server_shard = server.GetServerShard(conn->shard_id);
        server.ReleaseSendArgs(server_shard, args, flags);
        return;
    }

    server.OnSend(res, flags, *args);
}

void TcpServer::OnAccept(ServerShard& server_shard, int32 res, uint32 flags, CompletionArgs& args)
{
    static const std::unordered_set<int> fatal_errors =
    {
        EPERM, EBADF, EFAULT, EINVAL, ENOTSOCK, EOPNOTSUPP,
        EAFNOSUPPORT, EADDRINUSE, EADDRNOTAVAIL,
        ENETDOWN, ESHUTDOWN
    };

    if (res >= 0)
    {
        Connection* accepted_conn = server_shard.shard.AcquireConnection(static_cast<uint16>(Server::id), cfg.max_pending_send_buffers_per_connection);
        if (accepted_conn == nullptr)
        {
            Logging::Warning("[TcpServer] Connection pool is full on shard %d.", server_shard.shard.id);
            shutdown(res, SHUT_RDWR);
            close(res);
        }
        else
        {
            Connection& conn = *accepted_conn;
            conn.sock_fd = res;
            conn.recv_ctx.received_bytes = 0;
            conn.recv_ctx.header = { };
            conn.recv_ctx.buffer = server_shard.shard.framing_buffer_pool.Acquire();
            conn.send_ctx.pending_bytes = 0;
            conn.send_ctx.processed_bytes = 0;
            conn.send_ctx.active_buffer = nullptr;
            conn.send_ctx.pending_buffers.Reset();
            conn.queued_send_count.store(0, std::memory_order_release);
            conn.state.store(ConnectionState::Connected, std::memory_order_release);

            int no_delay = 1;
            setsockopt(conn.sock_fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));

            if (conn.recv_ctx.buffer == nullptr)
            {
                CloseInternal(server_shard, conn);
            }
            else
            {
                if (!server_shard.egress_connection_events.Enqueue({ ConnectionEventType::Connected, conn.CreateHandle() }))
                {
                    Logging::Warning("[TcpServer] Dropped connect event on shard %d.", server_shard.shard.id);
                }

                CompletionArgs* recv_args = server_shard.shard.args_pool.Acquire();
                if (recv_args == nullptr)
                {
                    CloseInternal(server_shard, conn);
                }
                else
                {
                    recv_args->data.recv.conn = &conn;
                    ReceiveInternal(server_shard, recv_args);
                }
            }
        }
    }

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        if (res >= 0)
        {
            AcceptInternal(server_shard, &args);
            return;
        }

        if (fatal_errors.contains(-res))
        {
            Logging::Error("[TcpServer] Failed to accept connection. Error: %s", strerror(-res));
            server_shard.shard.args_pool.Release(&args);
            return;
        }

        Logging::Warning("[TcpServer] Failed to accept connection. Error: %s", strerror(-res));
        AcceptInternal(server_shard, &args);
    }
}

void TcpServer::OnReceive(int32 res, uint32 flags, CompletionArgs& args)
{
    Connection* conn = args.data.recv.conn;
    assert(conn != nullptr && "[TcpServer] Receive completion args must bind a connection.");
    ServerShard& server_shard = GetServerShard(conn->shard_id);

    if (res <= 0)
    {
        Logging::Info("[TcpServer] Connection closed. Socket: %d, Error: %s", conn->sock_fd, strerror(-res));
        CloseInternal(server_shard, *conn);
        ReleaseReceiveArgs(server_shard, &args, flags);
        return;
    }

    if ((flags & IORING_CQE_F_BUFFER) == 0)
    {
        Logging::Error("[TcpServer] No buffer selected for received data.");
        CloseInternal(server_shard, *conn);
        ReleaseReceiveArgs(server_shard, &args, flags);
        return;
    }

    byte* recv_buf = server_shard.shard.AcquireBuffer(cfg.io_buf_gid, flags >> IORING_CQE_BUFFER_SHIFT);
    if (recv_buf == nullptr)
    {
        Logging::Error("[TcpServer] Invalid receive buffer group or buffer id.");
        CloseInternal(server_shard, *conn);
        ReleaseReceiveArgs(server_shard, &args, flags);
        return;
    }

    RecvContext& ctx = conn->recv_ctx;
    PacketSize pending_bytes = static_cast<PacketSize>(res);
    PacketSize offset = 0;
    while (pending_bytes > 0)
    {
        if (ctx.received_bytes < TRANSPORT_HEADER_SIZE)
        {
            const PacketSize header_needed = TRANSPORT_HEADER_SIZE - ctx.received_bytes;
            const PacketSize copied = std::min(header_needed, pending_bytes);
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
                CloseInternal(server_shard, *conn);
                ReleaseReceiveArgs(server_shard, &args, flags);
                return;
            }
        }

        const PacketSize frame_needed = ctx.header.length - ctx.received_bytes;
        const PacketSize copied = std::min(frame_needed, pending_bytes);
        std::memcpy(ctx.buffer + ctx.received_bytes, recv_buf + offset, copied);
        ctx.received_bytes += copied;
        pending_bytes -= copied;
        offset += copied;

        if (ctx.received_bytes < ctx.header.length)
        {
            break;
        }

        if (!HandlePacket(server_shard, ctx.header, ctx.buffer + TRANSPORT_HEADER_SIZE))
        {
            CloseInternal(server_shard, *conn);
            ReleaseReceiveArgs(server_shard, &args, flags);
            return;
        }

        ctx.received_bytes = 0;
        ctx.header = { };
    }

    server_shard.shard.ReleaseBuffer(cfg.io_buf_gid, flags >> IORING_CQE_BUFFER_SHIFT);
    ReceiveInternal(server_shard, &args);
}

void TcpServer::OnSend(int32 res, uint32 flags, CompletionArgs& args)
{
    Connection* conn = args.data.send.conn;
    assert(conn != nullptr && "[TcpServer] Send completion args must bind a connection.");
    ServerShard& server_shard = GetServerShard(conn->shard_id);

    if (res < 0)
    {
        Logging::Info("[TcpServer] Connection closed while sending. Socket: %d, Error: %s", conn->sock_fd, strerror(-res));
        CloseInternal(server_shard, *conn);
        ReleaseSendArgs(server_shard, &args, flags);
        return;
    }

    SendContext& ctx = conn->send_ctx;
    ctx.pending_bytes -= static_cast<PacketSize>(res);
    ctx.processed_bytes += static_cast<PacketSize>(res);
    if (ctx.pending_bytes > 0)
    {
        SendInternal(server_shard, &args, ctx.active_buffer + ctx.processed_bytes, ctx.pending_bytes);
        return;
    }

    ReleaseQueuedSend(*conn);
    ReleaseBufferReference(server_shard.shard, ctx.active_buffer);

    byte* next_buffer = nullptr;
    if (!ctx.pending_buffers.Pop(next_buffer))
    {
        ctx.active_buffer = nullptr;
        ctx.pending_bytes = 0;
        ctx.processed_bytes = 0;
        server_shard.shard.args_pool.Release(&args);
        FinalizeClosingConnection(server_shard, *conn);
        return;
    }

    ctx.active_buffer = next_buffer;
    ctx.pending_bytes = GetSendMetadata(next_buffer).size;
    ctx.processed_bytes = 0;
    SendInternal(server_shard, &args, next_buffer, ctx.pending_bytes);
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
