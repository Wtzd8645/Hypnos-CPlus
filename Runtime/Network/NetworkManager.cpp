#include "Hypnos/Network/NetworkManager.hpp"
#include "Hypnos/Network/ServerBase.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <Hypnos-Core/Base/Cpu/CpuUtils.hpp>
#include <cassert>
#include <cstring>
#include <limits>
#include <unistd.h>
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

Status<void> NetworkManager::Configure(const NetworkConfig& config)
{
    assert(io_ctx == nullptr && "[NetworkManager] Already configured.");

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

    const size_t endpoint_count = config.servers.size() + config.clients.size();
    if (endpoint_count > static_cast<size_t>(std::numeric_limits<uint8>::max()) + 1)
    {
        return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Endpoint count exceeds uint8 id range.");
    }

    if (!config.clients.empty())
    {
        return Status<void>::Error(ErrorCode::Unsupported, "[NetworkManager] Client configuration is not supported yet.");
    }

    io_ctx = new IOContext(config.io_uring);
    if (io_ctx->event_fd < 0)
    {
        Release();
        return Status<void>::Error(ErrorCode::InitializationFailed, "[NetworkManager] Failed to create eventfd.");
    }

    Status<void> status = io_ctx->Initialize(config.io_uring);
    if (status.IsFailed())
    {
        Release();
        return status;
    }

    for (const IOBufferConfig& cfg : config.io_buffers)
    {
        status = io_ctx->CreateBuffer(cfg);
        if (status.IsFailed())
        {
            Release();
            return status;
        }
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

        if (io_ctx->io_buffers[server_cfg.io_buf_gid] == nullptr)
        {
            Release();
            return Status<void>::Error(ErrorCode::ConfigurationError, "[NetworkManager] Server references unknown recv group.");
        }

        ServerBase* server = nullptr;
        switch (server_cfg.protocol)
        {
            case TransportProtocol::TCP:
            {
                server = new TcpServer(server_cfg.id, server_cfg, *io_ctx);
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
    assert(io_ctx != nullptr && "[NetworkManager] IO context should have been initialized during configuration.");
    assert(io_thread == nullptr && "[NetworkManager] IO thread should not be running before start.");
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

    uint32 cpu_ids[CpuUtils::MaxCpuIdCount];
    uint32 cpu_count = CpuUtils::GetAvailableCpuIds(cpu_ids);
    if (cpu_count == 0)
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

        return Status<void>::Error(ErrorCode::InitializationFailed, "[NetworkManager] Failed to discover available CPU ids.");
    }

    uint32 cpu_index = cfg.io_cpu_index < cpu_count ? cfg.io_cpu_index : cpu_count - 1;
    io_thread = new Thread(&NetworkManager::Process, this, cpu_ids[cpu_index]);
    return Status<void>::Success();
}

void NetworkManager::Stop()
{
    running.store(false, std::memory_order_release);
    if (io_ctx != nullptr && io_ctx->event_fd >= 0)
    {
        eventfd_write(io_ctx->event_fd, 1);
    }

    if (io_thread != nullptr)
    {
        io_thread->join();
        delete io_thread;
        io_thread = nullptr;
    }

    for (EndpointBase* endpoint : endpoints)
    {
        assert(endpoint != nullptr && "[NetworkManager] Endpoint should have been created during configuration.");
        endpoint->Stop();
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

    if (io_ctx != nullptr)
    {
        delete io_ctx;
        io_ctx = nullptr;
    }

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

void NetworkManager::Process(uint32 cpu_id)
{
    if (CpuUtils::PinThread(cpu_id).IsFailed())
    {
        Logging::Warning("[NetworkManager] Failed to pin IO thread to CPU.");
    }

    io_uring* ring = &io_ctx->ring;
    int32 res;
    io_uring_cqe* cqes;
    uint32 head;
    io_uring_cqe* cqe;
    running.store(true, std::memory_order_release);
    while (running.load(std::memory_order_acquire))
    {
        res = io_uring_wait_cqe(ring, &cqes);
        if (res < 0)
        {
            OnCqeError(-res);
            continue;
        }

        io_uring_for_each_cqe(ring, head, cqe)
        {
            IOEventArgs* args = static_cast<IOEventArgs*>(io_uring_cqe_get_data(cqe));
            endpoints[args->ep_id]->Process(IOEvent { cqe->res, cqe->flags, args });
        }

        io_ctx->FlushBuffers();
        io_ctx->AdvanceCqeRing(head);
    }
}

void NetworkManager::OnCqeError(int32 err)
{
    static const std::unordered_set<int> retriable_errors = { EIO, EAGAIN, ENOMEM, EBUSY };

    if (retriable_errors.contains(err))
    {
        Logging::Warning("[NetworkManager] Failed to wait for CQE. Error: %s", strerror(err));
        usleep(8); // TODO: Make configurable.
    }
    else
    {
        Logging::Error("[NetworkManager] Failed to wait for CQE. %s", strerror(err));
        terminal_error_code.store(static_cast<int32>(ErrorCode::IOError), std::memory_order_release);
        running.store(false, std::memory_order_release);
    }
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
