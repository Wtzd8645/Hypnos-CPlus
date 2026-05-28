#include "NetworkCore.hpp"

#include "DeliveryPump.hpp"
#include "SocketUtils_Unix.hpp"

#include <memory>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

Status<void> NetworkCore::Start()
{
#if !defined(__linux__)
    return NetworkError(NetworkStatus::Unsupported, "[NetworkManager] Epoll backend requires Linux.");
#else
    CleanupTransport();
    workers.clear();
    workers.resize(config.worker_count);

    for (uint16 i = 0; i < config.worker_count; ++i)
    {
        workers[i] = std::make_unique<EpollWorker>();
        Status<void> status = workers[i]->Initialize(i, WorkerCommandCapacity(i), WorkerDeliveryCapacity(i));
        if (status.IsFailed())
        {
            CleanupTransport();
            return status;
        }
    }

    for (UniquePtr<Endpoint>& endpoint : endpoints)
    {
        if (endpoint->kind == EndpointKind::Server)
        {
            if (endpoint->server != nullptr)
            {
                endpoint->server_state = ServerState::Starting;
            }

            Status<int32> listen_status = OpenListenSocket(endpoint->address);
            if (listen_status.IsFailed())
            {
                CleanupTransport();
                return Status<void>::Error(static_cast<ErrorCode>(listen_status.ErrorCode()), listen_status.Message());
            }

            endpoint->listen_fd = listen_status.Value();
            Status<void> add_status = workers[endpoint->worker_id]->AddListenSocket(endpoint->id, endpoint->listen_fd);
            if (add_status.IsFailed())
            {
                CleanupTransport();
                return add_status;
            }

            if (endpoint->server != nullptr)
            {
                endpoint->server_state = ServerState::Listening;
            }
        }
        else if (endpoint->client != nullptr)
        {
            endpoint->client_state = ClientState::Disconnected;
        }
    }

    for (UniquePtr<EpollWorker>& worker : workers)
    {
        worker->Start(*this);
    }

    return Status<void>::Success();
#endif
}

void NetworkCore::Stop()
{
    CleanupTransport();
}

void NetworkCore::DispatchCallbacks()
{
    RunDeliveryPump(*this);
}

void NetworkCore::CleanupTransport()
{
    ReleaseDeliveredMessages();

#if defined(__linux__)
    for (UniquePtr<EpollWorker>& worker : workers)
    {
        if (worker != nullptr)
        {
            worker->Stop();
        }
    }
#endif

    workers.clear();

    for (UniquePtr<Endpoint>& endpoint : endpoints)
    {
        endpoint->ResetTransport();
    }
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
