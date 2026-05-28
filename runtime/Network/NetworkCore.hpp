#pragma once

#include <Hypnos/Network/NetworkManager.hpp>

#include "Endpoint.hpp"
#include "EpollWorker_Unix.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct NetworkCore
{
    NetworkConfig config;
    List<Server> servers;
    List<Client> clients;
    List<UniquePtr<Endpoint>> endpoints;
    List<UniquePtr<EpollWorker>> workers;
    List<IMessage*> delivered_messages;
    Mutex mutex;
    ManagerState manager_state = ManagerState::Unconfigured;
    bool is_dispatching_callbacks = false;
    bool is_dispatching_message_callback = false;

    Endpoint* FindEndpoint(EndpointId id) const noexcept
    {
        if (id == INVALID_ENDPOINT_ID || static_cast<size_t>(id) >= endpoints.size())
        {
            return nullptr;
        }

        return endpoints[id].get();
    }

    Server* FindServer(EndpointId id) noexcept
    {
        Endpoint* endpoint = FindEndpoint(id);
        return endpoint != nullptr ? endpoint->server : nullptr;
    }

    Client* FindClient(EndpointId id) noexcept
    {
        Endpoint* endpoint = FindEndpoint(id);
        return endpoint != nullptr ? endpoint->client : nullptr;
    }

    void ReleaseDeliveredMessages()
    {
        if (config.message_allocator != nullptr)
        {
            for (IMessage* message : delivered_messages)
            {
                if (message != nullptr)
                {
                    config.message_allocator->Release(*message);
                }
            }
        }

        delivered_messages.clear();
    }

    size_t WorkerCommandCapacity(uint16 worker_id) const noexcept
    {
        size_t capacity = 1;
        for (const ServerConfig& server_config : config.servers)
        {
            if (server_config.worker_id == worker_id)
            {
                capacity += static_cast<size_t>(server_config.max_connections)
                    * (static_cast<size_t>(server_config.send_queue_capacity_per_connection) + 2);
            }
        }

        for (const ClientConfig& client_config : config.clients)
        {
            if (client_config.worker_id == worker_id)
            {
                capacity += static_cast<size_t>(client_config.send_queue_capacity) + 2;
            }
        }

        return capacity;
    }

    size_t WorkerDeliveryCapacity(uint16 worker_id) const noexcept
    {
        size_t capacity = 1;
        for (const ServerConfig& server_config : config.servers)
        {
            if (server_config.worker_id == worker_id)
            {
                capacity += server_config.delivery_queue_capacity;
            }
        }

        for (const ClientConfig& client_config : config.clients)
        {
            if (client_config.worker_id == worker_id)
            {
                capacity += client_config.delivery_queue_capacity;
            }
        }

        return capacity;
    }

    bool QueueDelivery(Endpoint& endpoint, const DeliveryEvent& event)
    {
#if defined(__linux__)
        EpollWorker* worker = workers[endpoint.worker_id].get();
        if (worker != nullptr && worker->delivery_queue != nullptr && worker->delivery_queue->Enqueue(event))
        {
            return true;
        }
#else
        (void)event;
#endif

        endpoint.EnqueueDeliveryOverflow(event);
        if (event.connection.IsValid())
        {
            ConnectionSlot* connection = endpoint.Resolve(event.connection);
            if (connection != nullptr)
            {
                connection->state = ConnectionState::Failed;
            }
        }
        else if (endpoint.kind == EndpointKind::Server)
        {
            endpoint.server_state = ServerState::Failed;
        }
        else if (endpoint.kind == EndpointKind::Client)
        {
            endpoint.client_state = ClientState::Failed;
        }

        return false;
    }

    Status<void> Start();
    void Stop();
    void DispatchCallbacks();
    void CleanupTransport();
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
