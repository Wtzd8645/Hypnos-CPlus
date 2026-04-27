#include "Hypnos/Network/NetworkManager.hpp"
#include "Hypnos/Network/Server.hpp"
#include "EndpointRuntime.hpp"
#include "NetworkShard.hpp"
#include "TcpServer.hpp"
#include <Hypnos-Core/Base/Cpu/CpuUtils.hpp>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <limits>
#include <sys/eventfd.h>
#include <unistd.h>
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

namespace {

size_t ResolveEndpointShardCapacity(size_t total_capacity, size_t bound_shard_count, size_t bound_shard_index) noexcept
{
    if (bound_shard_count == 0)
    {
        return 0;
    }

    const size_t base = total_capacity / bound_shard_count;
    const size_t remainder = total_capacity % bound_shard_count;
    return base + (bound_shard_index < remainder ? 1 : 0);
}

size_t ResolveArgsCapacity(size_t connection_capacity) noexcept
{
    return connection_capacity * 2 + 1 + 8;
}

uint16 ResolveShardCount(const NetworkConfig& config, uint32 available_cpu_count) noexcept
{
    if (!config.shard_cpu_ids.empty())
    {
        if (config.shard_count == 0)
        {
            return static_cast<uint16>(config.shard_cpu_ids.size());
        }

        return static_cast<uint16>(std::min<size_t>(config.shard_count, config.shard_cpu_ids.size()));
    }

    if (config.shard_count != 0)
    {
        return static_cast<uint16>(std::min<uint32>(config.shard_count, available_cpu_count));
    }

    if (available_cpu_count <= 1)
    {
        return 1;
    }

    return static_cast<uint16>(available_cpu_count - 1);
}

} // namespace

Status<void> NetworkManager::Configure(const NetworkConfig& config)
{
    assert(shards.empty() && "[NetworkManager] Already configured.");

    if (config.io_uring.sq_entries == 0)
    {
        return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] io_uring sq_entries must be greater than zero.");
    }

    if (config.io_buffers.empty())
    {
        return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] At least one recv buffer group is required.");
    }

    if (config.servers.empty() && config.clients.empty())
    {
        return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] At least one endpoint (server or client) is required.");
    }

    if (!config.clients.empty())
    {
        return Status<void>::Error(ErrorCode::Unsupported, "[NetworkManager] Client configuration is not supported yet.");
    }

    const size_t endpoint_count = config.servers.size() + config.clients.size();
    if (endpoint_count > static_cast<size_t>(std::numeric_limits<uint8>::max()) + 1)
    {
        return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Endpoint count exceeds uint8 id range.");
    }

    uint32 cpu_ids[CpuUtils::MaxCpuIdCount];
    const uint32 available_cpu_count = CpuUtils::GetAvailableCpuIds(cpu_ids);
    if (available_cpu_count == 0)
    {
        return Status<void>::Error(ErrorCode::InitializationFailed, "[NetworkManager] Failed to discover available CPU ids.");
    }

    const uint16 shard_count = ResolveShardCount(config, available_cpu_count);
    if (shard_count == 0)
    {
        return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Shard count must be greater than zero.");
    }

    shard_cpu_ids.clear();
    shard_endpoints.clear();
    if (!config.shard_cpu_ids.empty())
    {
        shard_cpu_ids.assign(config.shard_cpu_ids.begin(), config.shard_cpu_ids.begin() + shard_count);
    }
    else
    {
        shard_cpu_ids.assign(cpu_ids, cpu_ids + shard_count);
    }

    List<size_t> shard_args_pool_capacities(shard_count, 0);
    List<size_t> shard_framing_pool_capacities(shard_count, 0);
    List<size_t> shard_send_pool_capacities(shard_count, 0);
    List<size_t> shard_connection_capacities(shard_count, 0);
    List<uint32> shard_pending_send_capacities(shard_count, 1);

    for (const ServerConfig& server_cfg : config.servers)
    {
        if (server_cfg.shard_ids.empty())
        {
            Release();
            return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Server must bind at least one shard.");
        }

        List<bool> shard_seen(shard_count, false);
        for (uint16 shard_id : server_cfg.shard_ids)
        {
            if (shard_id >= shard_count)
            {
                Release();
                return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Server references an out-of-range shard id.");
            }

            if (shard_seen[shard_id])
            {
                Release();
                return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Server shard_ids must not contain duplicates.");
            }

            shard_seen[shard_id] = true;
        }

        const size_t bound_shard_count = server_cfg.shard_ids.size();
        for (size_t bound_shard_index = 0; bound_shard_index < bound_shard_count; ++bound_shard_index)
        {
            const uint16 shard_id = server_cfg.shard_ids[bound_shard_index];
            const size_t connection_capacity = ResolveEndpointShardCapacity(static_cast<size_t>(server_cfg.max_conns > 0 ? server_cfg.max_conns : 1), bound_shard_count, bound_shard_index);
            const size_t send_buffer_capacity = ResolveEndpointShardCapacity(server_cfg.send_buffer_pool_capacity, bound_shard_count, bound_shard_index);

            shard_connection_capacities[shard_id] += connection_capacity;
            shard_framing_pool_capacities[shard_id] += connection_capacity;
            shard_send_pool_capacities[shard_id] += send_buffer_capacity;
            shard_args_pool_capacities[shard_id] += ResolveArgsCapacity(connection_capacity);
            shard_pending_send_capacities[shard_id] = std::max<uint32>(shard_pending_send_capacities[shard_id], server_cfg.max_pending_send_buffers_per_connection);
        }
    }

    for (uint16 shard_id = 0; shard_id < shard_count; ++shard_id)
    {
        if (shard_connection_capacities[shard_id] > std::numeric_limits<uint16>::max())
        {
            Release();
            return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Shard connection capacity exceeds ConnectionHandle slot range.");
        }
    }

    shards.reserve(shard_count);
    for (uint16 shard_id = 0; shard_id < shard_count; ++shard_id)
    {
        NetworkShard* shard = new NetworkShard(config.io_uring,
                                              shard_args_pool_capacities[shard_id],
                                              shard_framing_pool_capacities[shard_id],
                                              shard_send_pool_capacities[shard_id],
                                              shard_connection_capacities[shard_id],
                                              shard_pending_send_capacities[shard_id]);
        if (shard->event_fd < 0)
        {
            delete shard;
            Release();
            return Status<void>::Error(ErrorCode::InitializationFailed, "[NetworkManager] Failed to create eventfd.");
        }

        Status<void> status = shard->Initialize(config.io_uring, shard_id);
        if (status.IsFailed())
        {
            delete shard;
            Release();
            return status;
        }

        shard->cpu_id = shard_cpu_ids[shard_id];
        shard->wake_poll_args.complete = &NetworkManager::OnWakeCqe;
        shard->wake_poll_args.owner = this;
        shard->wake_poll_args.type = CompletionArgs::Type::Wake;
        shard->wake_poll_args.data.wake.shard_id = shard_id;

        for (const IOBufferConfig& io_buf_cfg : config.io_buffers)
        {
            status = shard->CreateBuffer(io_buf_cfg);
            if (status.IsFailed())
            {
                delete shard;
                Release();
                return status;
            }
        }

        shards.push_back(shard);
    }

    shard_endpoints.resize(shard_count);
    endpoints.assign(endpoint_count, nullptr);
    for (const ServerConfig& server_cfg : config.servers)
    {
        const size_t endpoint_id = static_cast<size_t>(server_cfg.id);
        if (endpoint_id >= endpoint_count)
        {
            Release();
            return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Endpoint id must be contiguous in [0, endpoint_count).");
        }

        if (endpoints[endpoint_id] != nullptr)
        {
            Release();
            return Status<void>::Error(ErrorCode::AlreadyExists, "[NetworkManager] Duplicate endpoint id across servers/clients.");
        }

        const size_t min_send_buffer_size = BufferMetadata::SEND_OFFSET + sizeof(BufferMetadata);
        if (server_cfg.send_buffer_size != MAX_BUFFER_SIZE ||
            server_cfg.send_buffer_size < min_send_buffer_size ||
            server_cfg.send_buffer_pool_capacity == 0 ||
            server_cfg.max_pending_send_buffers_per_connection == 0)
        {
            Release();
            return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Server send buffer settings are invalid.");
        }

        for (uint16 shard_id : server_cfg.shard_ids)
        {
            NetworkShard* shard = shards[shard_id];
            if (shard->io_buffers[server_cfg.io_buf_gid] == nullptr)
            {
                Release();
                return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Server references unknown recv group.");
            }
        }

        EndpointRuntime* endpoint = nullptr;
        switch (server_cfg.protocol)
        {
            case TransportProtocol::Tcp:
            {
                List<NetworkShard*> bound_shards;
                bound_shards.reserve(server_cfg.shard_ids.size());
                for (uint16 shard_id : server_cfg.shard_ids)
                {
                    bound_shards.push_back(shards[shard_id]);
                }

                endpoint = new TcpServer(server_cfg.id, server_cfg, shard_count, bound_shards);
                break;
            }
            default:
            {
                Release();
                return Status<void>::Error(ErrorCode::Unsupported, "[NetworkManager] Unsupported transport protocol for server.");
            }
        }

        endpoints[endpoint_id] = endpoint;
        for (uint16 shard_id : server_cfg.shard_ids)
        {
            shard_endpoints[shard_id].push_back(endpoint);
        }
    }

    for (size_t endpoint_id = 0; endpoint_id < endpoint_count; ++endpoint_id)
    {
        if (endpoints[endpoint_id] == nullptr)
        {
            Release();
            return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Endpoint ids must be contiguous and shared between servers and clients.");
        }
    }

    cfg = config;
    return Status<void>::Success();
}

Status<void> NetworkManager::Start()
{
    assert(ClaimOwnerThread() && "[NetworkManager] Start must be called from the owner thread.");
    assert(!shards.empty() && "[NetworkManager] Shards should have been initialized during configuration.");

    auto rollback_started_endpoints = [this](size_t started_count)
    {
        while (started_count > 0)
        {
            EndpointRuntime* started_endpoint = endpoints[--started_count];
            Status<void> stop_status = started_endpoint->Stop();
            if (stop_status.IsFailed())
            {
                Logging::Warning("[NetworkManager] Failed to rollback endpoint %d. %s", started_endpoint->id, stop_status.Message());
            }
        }
    };

    for (NetworkShard* shard : shards)
    {
        Status<void> status = shard->AllocateSharedResources();
        if (status.IsFailed())
        {
            return status;
        }
    }

    size_t started_count = 0;
    for (; started_count < endpoints.size(); ++started_count)
    {
        EndpointRuntime* endpoint = endpoints[started_count];
        assert(endpoint != nullptr && "[NetworkManager] Endpoint should have been created during configuration.");
        Status<void> status = endpoint->Start();
        if (status.IsFailed())
        {
            rollback_started_endpoints(started_count);
            return status;
        }
    }

    for (NetworkShard* shard : shards)
    {
        Status<void> status = shard->SubmitWakePoll();
        if (status.IsFailed())
        {
            rollback_started_endpoints(started_count);
            return status;
        }
    }

    for (NetworkShard* shard : shards)
    {
        if (io_uring_submit(&shard->ring) < 0)
        {
            rollback_started_endpoints(started_count);
            return Status<void>::Error(ErrorCode::IOError, "[NetworkManager] Failed to submit initial io_uring SQEs.");
        }
    }

    terminal_error_code.store(0, std::memory_order_release);
    running.store(true, std::memory_order_release);
    for (NetworkShard* shard : shards)
    {
        shard->state.store(NetworkShard::State::Running, std::memory_order_release);
        shard->thread = new Thread(&NetworkManager::RunShardLoop, this, std::ref(*shard));
    }

    return Status<void>::Success();
}

void NetworkManager::RunShardLoop(NetworkShard& shard)
{
    if (CpuUtils::PinThread(shard.cpu_id).IsFailed())
    {
        Logging::Warning("[NetworkManager] Failed to pin network shard thread to CPU %d.", shard.cpu_id);
    }

    io_uring* ring = &shard.ring;
    while (running.load(std::memory_order_acquire))
    {
        io_uring_cqe* cqe = nullptr;
        const int32 res = io_uring_wait_cqe(ring, &cqe);
        if (res < 0)
        {
            OnCqeError(-res);
            continue;
        }

        uint32 consumed_count = 0;
        uint32 head = 0;
        io_uring_for_each_cqe(ring, head, cqe)
        {
            ++consumed_count;
            CompletionArgs* args = static_cast<CompletionArgs*>(io_uring_cqe_get_data(cqe));
            if (args == nullptr || args->complete == nullptr)
            {
                OnCqeError(0);
                break;
            }

            args->complete(cqe->res, cqe->flags, args);
            if (!running.load(std::memory_order_acquire))
            {
                break;
            }
        }

        shard.FlushBuffers();
        shard.AdvanceCqeRing(consumed_count);
        if (io_uring_sq_ready(ring) > 0)
        {
            io_uring_submit(ring);
        }
    }
}

bool NetworkManager::HandleWakeCqe(NetworkShard& shard, uint32 flags)
{
    shard.ClearWakeEvent();
    while (shard.ConsumePendingCommands() > 0)
    {
        for (EndpointRuntime* endpoint : shard_endpoints[shard.id])
        {
            endpoint->OnShardWake(shard.id);
        }
    }

    if ((flags & IORING_CQE_F_MORE) == 0 && running.load(std::memory_order_acquire))
    {
        Status<void> status = shard.SubmitWakePoll();
        if (status.IsFailed())
        {
            Logging::Error("[NetworkManager] Failed to resubmit shard wake poll. %s", status.Message());
            terminal_error_code.store(static_cast<int32>(ErrorCode::IOError), std::memory_order_release);
            running.store(false, std::memory_order_release);
            WakeShards();
            return false;
        }

        if (io_uring_submit(&shard.ring) < 0)
        {
            Logging::Error("[NetworkManager] Failed to submit shard wake poll SQE.");
            terminal_error_code.store(static_cast<int32>(ErrorCode::IOError), std::memory_order_release);
            running.store(false, std::memory_order_release);
            WakeShards();
            return false;
        }
    }

    return true;
}

void NetworkManager::Stop()
{
    assert(ClaimOwnerThread() && "[NetworkManager] Stop must be called from the owner thread.");

    running.store(false, std::memory_order_release);
    for (NetworkShard* shard : shards)
    {
        shard->state.store(NetworkShard::State::Stopping, std::memory_order_release);
    }
    WakeShards();

    for (NetworkShard* shard : shards)
    {
        if (shard->thread != nullptr)
        {
            shard->thread->join();
            delete shard->thread;
            shard->thread = nullptr;
        }

        shard->state.store(NetworkShard::State::Stopped, std::memory_order_release);
    }

    for (EndpointRuntime* endpoint : endpoints)
    {
        if (endpoint != nullptr)
        {
            endpoint->Stop();
        }
    }
}

void NetworkManager::Release()
{
    Stop();

    for (EndpointRuntime* endpoint : endpoints)
    {
        delete endpoint;
    }
    endpoints.clear();

    for (NetworkShard* shard : shards)
    {
        delete shard;
    }
    shards.clear();
    shard_endpoints.clear();
    shard_cpu_ids.clear();

    cfg = NetworkConfig { };
    owner_thread_id = std::thread::id();
    terminal_error_code.store(0, std::memory_order_release);
}

Status<void> NetworkManager::Update()
{
    assert(ClaimOwnerThread() && "[NetworkManager] Update must be called from the owner thread.");

    const int32 error_code = terminal_error_code.load(std::memory_order_acquire);
    if (error_code != 0)
    {
        return Status<void>::Error(static_cast<ErrorCode>(error_code), "[NetworkManager] Fatal IO loop failure.");
    }

    for (NetworkShard* shard : shards)
    {
        shard->DrainReturnedSendBuffers();
    }

    for (EndpointRuntime* endpoint : endpoints)
    {
        assert(endpoint != nullptr && "[NetworkManager] Endpoint should have been created during configuration.");
        endpoint->Dispatch();
    }

    return Status<void>::Success();
}

void NetworkManager::OnWakeCqe(int32 res, uint32 flags, CompletionArgs* args)
{
    (void)res;
    assert(args != nullptr && "[NetworkManager] Wake CQE args must not be null.");
    assert(args->owner != nullptr && "[NetworkManager] Wake CQE owner must be bound.");
    assert(args->type == CompletionArgs::Type::Wake && "[NetworkManager] Unexpected completion args type for wake CQE.");

    NetworkManager& self = *static_cast<NetworkManager*>(args->owner);
    NetworkShard& shard = *self.shards[args->data.wake.shard_id];
    self.HandleWakeCqe(shard, flags);
}

void NetworkManager::WakeShards()
{
    for (NetworkShard* shard : shards)
    {
        if (shard != nullptr && shard->event_fd >= 0)
        {
            eventfd_write(shard->event_fd, 1);
        }
    }
}

void NetworkManager::OnCqeError(int32 err)
{
    static const std::unordered_set<int> retriable_errors = { EIO, EAGAIN, ENOMEM, EBUSY };

    if (retriable_errors.contains(err))
    {
        Logging::Warning("[NetworkManager] Failed to wait for CQE. Error: %s", strerror(err));
        usleep(8);
        return;
    }

    Logging::Error("[NetworkManager] Failed to wait for CQE. %s", strerror(err));
    terminal_error_code.store(static_cast<int32>(ErrorCode::IOError), std::memory_order_release);
    running.store(false, std::memory_order_release);
    WakeShards();
}

bool NetworkManager::ClaimOwnerThread() noexcept
{
    const std::thread::id current_thread_id = std::this_thread::get_id();
    if (owner_thread_id == std::thread::id())
    {
        owner_thread_id = current_thread_id;
        return true;
    }

    return owner_thread_id == current_thread_id;
}

Server* NetworkManager::GetServer(uint8 id) const
{
    const size_t endpoint_id = static_cast<size_t>(id);
    if (endpoint_id >= endpoints.size())
    {
        return nullptr;
    }

    EndpointRuntime* endpoint = endpoints[endpoint_id];
    if (endpoint == nullptr || endpoint->type != EndpointRuntime::Type::Server)
    {
        return nullptr;
    }

    return endpoint->AsServer();
}

Client* NetworkManager::GetClient(uint8 id) const
{
    const size_t endpoint_id = static_cast<size_t>(id);
    if (endpoint_id >= endpoints.size())
    {
        return nullptr;
    }

    EndpointRuntime* endpoint = endpoints[endpoint_id];
    if (endpoint == nullptr || endpoint->type != EndpointRuntime::Type::Client)
    {
        return nullptr;
    }

    return endpoint->AsClient();
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
