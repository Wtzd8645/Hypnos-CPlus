#include "Hypnos/Network/NetworkManager.hpp"
#include "Hypnos/Network/ServerBase.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <Hypnos-Core/Base/Cpu/CpuUtils.hpp>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <sys/eventfd.h>
#include <unistd.h>
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

namespace {

uint16 ResolveReactorCount(const NetworkConfig& config, uint32 available_cpu_count) noexcept
{
    if (!config.io_cpu_ids.empty())
    {
        if (config.reactor_count == 0)
        {
            return static_cast<uint16>(config.io_cpu_ids.size());
        }

        return static_cast<uint16>(std::min<size_t>(config.reactor_count, config.io_cpu_ids.size()));
    }

    if (config.reactor_count != 0)
    {
        return static_cast<uint16>(std::min<uint32>(config.reactor_count, available_cpu_count));
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
    assert(io_contexts.empty() && "[NetworkManager] Already configured.");

    if (config.io_uring.sq_entries == 0)
    {
        return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] io_uring sq_entries must be greater than zero.");
    }

    if (config.io_uring.send_pool_capacity == 0 || config.io_uring.send_buffer_size == 0)
    {
        return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Send buffer pool settings are invalid.");
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

    const uint16 reactor_count = ResolveReactorCount(config, available_cpu_count);
    if (reactor_count == 0)
    {
        return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Reactor count must be greater than zero.");
    }

    reactor_cpu_ids.clear();
    if (!config.io_cpu_ids.empty())
    {
        reactor_cpu_ids.assign(config.io_cpu_ids.begin(), config.io_cpu_ids.begin() + reactor_count);
    }
    else
    {
        reactor_cpu_ids.assign(cpu_ids, cpu_ids + reactor_count);
    }

    io_contexts.reserve(reactor_count);
    for (uint16 rid = 0; rid < reactor_count; ++rid)
    {
        IOContext* io_ctx = new IOContext(config.io_uring);
        if (io_ctx->event_fd < 0)
        {
            delete io_ctx;
            Release();
            return Status<void>::Error(ErrorCode::InitializationFailed, "[NetworkManager] Failed to create eventfd.");
        }

        Status<void> status = io_ctx->Initialize(config.io_uring, rid);
        if (status.IsFailed())
        {
            delete io_ctx;
            Release();
            return status;
        }

        io_ctx->wake_args.complete = &NetworkManager::OnWakeCqe;
        io_ctx->wake_args.owner = this;

        for (const IOBufferConfig& io_buf_cfg : config.io_buffers)
        {
            status = io_ctx->CreateBuffer(io_buf_cfg);
            if (status.IsFailed())
            {
                delete io_ctx;
                Release();
                return status;
            }
        }

        io_contexts.push_back(io_ctx);
    }

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

        for (IOContext* io_ctx : io_contexts)
        {
            if (io_ctx->io_buffers[server_cfg.io_buf_gid] == nullptr)
            {
                Release();
                return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Server references unknown recv group.");
            }
        }

        ServerBase* server = nullptr;
        switch (server_cfg.protocol)
        {
            case TransportProtocol::TCP:
            {
                server = new TcpServer(server_cfg.id, server_cfg, io_contexts);
                break;
            }
            default:
            {
                Release();
                return Status<void>::Error(ErrorCode::Unsupported, "[NetworkManager] Unsupported transport protocol for server.");
            }
        }

        endpoints[endpoint_id] = server;
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
    assert(!io_contexts.empty() && "[NetworkManager] IO contexts should have been initialized during configuration.");
    assert(io_threads.empty() && "[NetworkManager] IO threads should not be running before start.");

    size_t started_count = 0;
    for (; started_count < endpoints.size(); ++started_count)
    {
        EndpointBase* endpoint = endpoints[started_count];
        assert(endpoint != nullptr && "[NetworkManager] Endpoint should have been created during configuration.");
        Status<void> status = endpoint->Start();
        if (status.IsFailed())
        {
            while (started_count > 0)
            {
                EndpointBase* started_endpoint = endpoints[--started_count];
                Status<void> stop_status = started_endpoint->Stop();
                if (stop_status.IsFailed())
                {
                    Logging::Warning("[NetworkManager] Failed to rollback endpoint %d. %s", started_endpoint->id, stop_status.Message());
                }
            }

            return status;
        }
    }

    for (IOContext* io_ctx : io_contexts)
    {
        Status<void> status = io_ctx->SubmitWakePoll();
        if (status.IsFailed())
        {
            while (started_count > 0)
            {
                EndpointBase* started_endpoint = endpoints[--started_count];
                Status<void> stop_status = started_endpoint->Stop();
                if (stop_status.IsFailed())
                {
                    Logging::Warning("[NetworkManager] Failed to rollback endpoint %d. %s", started_endpoint->id, stop_status.Message());
                }
            }

            return status;
        }

        if (io_uring_submit(&io_ctx->ring) < 0)
        {
            while (started_count > 0)
            {
                EndpointBase* started_endpoint = endpoints[--started_count];
                Status<void> stop_status = started_endpoint->Stop();
                if (stop_status.IsFailed())
                {
                    Logging::Warning("[NetworkManager] Failed to rollback endpoint %d. %s", started_endpoint->id, stop_status.Message());
                }
            }

            return Status<void>::Error(ErrorCode::IOError, "[NetworkManager] Failed to submit initial io_uring SQEs.");
        }
    }

    running.store(true, std::memory_order_release);
    io_threads.reserve(io_contexts.size());
    for (uint16 rid = 0; rid < io_contexts.size(); ++rid)
    {
        io_threads.push_back(new Thread(&NetworkManager::Process, this, rid, reactor_cpu_ids[rid]));
    }

    return Status<void>::Success();
}

void NetworkManager::Stop()
{
    running.store(false, std::memory_order_release);
    WakeReactors();

    for (Thread* io_thread : io_threads)
    {
        if (io_thread != nullptr)
        {
            io_thread->join();
            delete io_thread;
        }
    }
    io_threads.clear();

    for (EndpointBase* endpoint : endpoints)
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

    for (EndpointBase* endpoint : endpoints)
    {
        delete endpoint;
    }
    endpoints.clear();

    for (IOContext* io_ctx : io_contexts)
    {
        delete io_ctx;
    }
    io_contexts.clear();
    reactor_cpu_ids.clear();

    cfg = NetworkConfig { };
    terminal_error_code.store(0, std::memory_order_release);
}

Status<void> NetworkManager::Update()
{
    const int32 error_code = terminal_error_code.load(std::memory_order_acquire);
    if (error_code != 0)
    {
        return Status<void>::Error(static_cast<ErrorCode>(error_code), "[NetworkManager] Fatal IO loop failure.");
    }

    for (EndpointBase* endpoint : endpoints)
    {
        assert(endpoint != nullptr && "[NetworkManager] Endpoint should have been created during configuration.");
        endpoint->Dispatch();
    }

    return Status<void>::Success();
}

void NetworkManager::Process(uint16 rid, uint32 cpu_id)
{
    if (CpuUtils::PinThread(cpu_id).IsFailed())
    {
        Logging::Warning("[NetworkManager] Failed to pin IO thread to CPU %d.", cpu_id);
    }

    IOContext& io_ctx = *io_contexts[rid];
    io_uring* ring = &io_ctx.ring;
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

        io_ctx.FlushBuffers();
        io_ctx.AdvanceCqeRing(consumed_count);
        if (io_uring_sq_ready(ring) > 0)
        {
            io_uring_submit(ring);
        }
    }
}

void NetworkManager::OnWakeCqe(int32 res, uint32 flags, CompletionArgs* args)
{
    (void)res;
    assert(args != nullptr && "[NetworkManager] Wake CQE args must not be null.");
    assert(args->owner != nullptr && "[NetworkManager] Wake CQE owner must be bound.");

    NetworkManager& self = *static_cast<NetworkManager*>(args->owner);
    IOContext& io_ctx = *self.io_contexts[args->rid];
    self.HandleWakeCqe(io_ctx, args->rid, flags);
}

bool NetworkManager::HandleWakeCqe(IOContext& io_ctx, uint16 rid, uint32 flags)
{
    io_ctx.ClearWakeEvent();
    while (io_ctx.ConsumePendingCommands() > 0)
    {
        for (EndpointBase* endpoint : endpoints)
        {
            endpoint->OnReactorWake(rid);
        }
    }

    if ((flags & IORING_CQE_F_MORE) == 0 && running.load(std::memory_order_acquire))
    {
        Status<void> status = io_ctx.SubmitWakePoll();
        if (status.IsFailed())
        {
            Logging::Error("[NetworkManager] Failed to resubmit reactor wake poll. %s", status.Message());
            terminal_error_code.store(static_cast<int32>(ErrorCode::IOError), std::memory_order_release);
            running.store(false, std::memory_order_release);
            WakeReactors();
            return false;
        }
    }

    return true;
}

void NetworkManager::WakeReactors()
{
    for (IOContext* io_ctx : io_contexts)
    {
        if (io_ctx != nullptr && io_ctx->event_fd >= 0)
        {
            eventfd_write(io_ctx->event_fd, 1);
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
    WakeReactors();
}

ServerBase* NetworkManager::GetServer(uint8 id) const
{
    const size_t endpoint_id = static_cast<size_t>(id);
    if (endpoint_id >= endpoints.size())
    {
        return nullptr;
    }

    EndpointBase* endpoint = endpoints[endpoint_id];
    if (endpoint == nullptr || endpoint->type != EndpointBase::Type::Server)
    {
        return nullptr;
    }

    return static_cast<ServerBase*>(endpoint);
}

ClientBase* NetworkManager::GetClient(uint8 id) const
{
    const size_t endpoint_id = static_cast<size_t>(id);
    if (endpoint_id >= endpoints.size())
    {
        return nullptr;
    }

    EndpointBase* endpoint = endpoints[endpoint_id];
    if (endpoint == nullptr || endpoint->type != EndpointBase::Type::Client)
    {
        return nullptr;
    }

    return static_cast<ClientBase*>(endpoint);
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
