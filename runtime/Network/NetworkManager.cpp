#include <Hypnos/Network/NetworkManager.hpp>

#include "NetworkCore.hpp"

#include <cassert>

namespace Blanketmen {
namespace Hypnos {
namespace Network {
NetworkManager::NetworkManager() :
    core(new NetworkCore())
{
}

NetworkManager::~NetworkManager()
{
    if (core != nullptr)
    {
        Release();
        delete static_cast<NetworkCore*>(core);
        core = nullptr;
    }
}

ManagerState NetworkManager::State() const noexcept
{
    const NetworkCore* network_core = static_cast<const NetworkCore*>(core);
    if (network_core != nullptr && network_core->owner_thread_id != std::thread::id { } && !IsOwnerThread())
    {
        assert(false && "[NetworkManager] State must be called from the owner thread.");
        return ManagerState::Unconfigured;
    }

    return network_core != nullptr ? network_core->manager_state : ManagerState::Unconfigured;
}

Status<void> NetworkManager::Configure(NetworkConfig&& network_config)
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    BindOwnerThread();

    Status<void> owner_status = CheckOwnerThread("[NetworkManager] Configure must be called from the owner thread.");
    if (owner_status.IsFailed())
    {
        return owner_status;
    }

    if (network_core->is_dispatching_callbacks)
    {
        return NetworkError(NetworkStatus::InvalidState, "[NetworkManager] Configure is not allowed during callback dispatch.");
    }

    if (network_core->manager_state != ManagerState::Unconfigured)
    {
        return NetworkError(NetworkStatus::InvalidState, "[NetworkManager] Configure is only valid while unconfigured.");
    }

    Status<void> validation_status = ValidateConfig(network_config);
    if (validation_status.IsFailed())
    {
        return validation_status;
    }

    network_core->config = std::move(network_config);
    network_core->servers.clear();
    network_core->clients.clear();
    network_core->endpoints.clear();

    network_core->servers.resize(network_core->config.servers.size());
    for (size_t i = 0; i < network_core->config.servers.size(); ++i)
    {
        Server& server = network_core->servers[i];
        server.endpoint = nullptr;

        auto endpoint = std::make_unique<Endpoint>();
        Status<void> status = endpoint->InitializeServer(*network_core, server, network_core->config.servers[i]);
        if (status.IsFailed())
        {
            Release();
            return status;
        }

        server.endpoint = endpoint.get();
        network_core->endpoints.push_back(std::move(endpoint));
    }

    network_core->clients.resize(network_core->config.clients.size());
    for (size_t i = 0; i < network_core->config.clients.size(); ++i)
    {
        Client& client = network_core->clients[i];
        client.endpoint = nullptr;

        auto endpoint = std::make_unique<Endpoint>();
        Status<void> status = endpoint->InitializeClient(*network_core, client, network_core->config.clients[i]);
        if (status.IsFailed())
        {
            Release();
            return status;
        }

        client.endpoint = endpoint.get();
        network_core->endpoints.push_back(std::move(endpoint));
    }

    network_core->manager_state = ManagerState::Configured;
    return Status<void>::Success();
}

Status<void> NetworkManager::Start()
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    BindOwnerThread();

    Status<void> owner_status = CheckOwnerThread("[NetworkManager] Start must be called from the owner thread.");
    if (owner_status.IsFailed())
    {
        return owner_status;
    }

    if (network_core->is_dispatching_callbacks)
    {
        return NetworkError(NetworkStatus::InvalidState, "[NetworkManager] Start is not allowed during callback dispatch.");
    }

    if (network_core->manager_state == ManagerState::Unconfigured)
    {
        return NetworkError(NetworkStatus::NotReady, "[NetworkManager] Start requires configured endpoints.");
    }

    if (network_core->manager_state != ManagerState::Configured)
    {
        return NetworkError(NetworkStatus::InvalidState, "[NetworkManager] Start is only valid while configured.");
    }

    Status<void> start_status = network_core->Start();
    if (start_status.IsFailed())
    {
        return start_status;
    }

    network_core->manager_state = ManagerState::Running;
    return Status<void>::Success();
}

Status<void> NetworkManager::Stop()
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    BindOwnerThread();

    Status<void> owner_status = CheckOwnerThread("[NetworkManager] Stop must be called from the owner thread.");
    if (owner_status.IsFailed())
    {
        return owner_status;
    }

    if (network_core->is_dispatching_callbacks)
    {
        return NetworkError(NetworkStatus::InvalidState, "[NetworkManager] Stop is not allowed during callback dispatch.");
    }

    if (network_core->manager_state == ManagerState::Unconfigured || network_core->manager_state == ManagerState::Configured)
    {
        return Status<void>::Success();
    }

    if (network_core->manager_state != ManagerState::Running)
    {
        return NetworkError(NetworkStatus::InvalidState, "[NetworkManager] Stop is only valid while running.");
    }

    network_core->manager_state = ManagerState::Stopping;
    network_core->is_dispatching_callbacks = false;
    network_core->is_dispatching_message_callback = false;
    Status<void> stop_status = network_core->Stop();
    if (stop_status.IsFailed())
    {
        return stop_status;
    }

    network_core->manager_state = ManagerState::Configured;
    return Status<void>::Success();
}

void NetworkManager::Release()
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    if (network_core != nullptr)
    {
        if (network_core->owner_thread_id != std::thread::id { } && !IsOwnerThread())
        {
            assert(false && "[NetworkManager] Release must be called from the owner thread.");
            return;
        }

        if (network_core->is_dispatching_callbacks)
        {
            assert(false && "[NetworkManager] Release is not allowed during callback dispatch.");
            return;
        }

        network_core->is_dispatching_callbacks = false;
        network_core->is_dispatching_message_callback = false;
        network_core->CleanupTransport();
        network_core->config = NetworkConfig { };
        network_core->servers.clear();
        network_core->clients.clear();
        network_core->endpoints.clear();
        network_core->workers.clear();
        network_core->delivered_messages.clear();
        network_core->manager_state = ManagerState::Unconfigured;
        network_core->owner_thread_id = std::thread::id { };
    }
}

Status<void> NetworkManager::Update()
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    BindOwnerThread();

    Status<void> owner_status = CheckOwnerThread("[NetworkManager] Update must be called from the owner thread.");
    if (owner_status.IsFailed())
    {
        return owner_status;
    }

    if (network_core->is_dispatching_callbacks)
    {
        return NetworkError(NetworkStatus::InvalidState, "[NetworkManager] Update is not allowed during callback dispatch.");
    }

    if (network_core->manager_state != ManagerState::Running)
    {
        return NetworkError(NetworkStatus::NotReady, "[NetworkManager] Update requires a running manager.");
    }

    network_core->ReleaseDeliveredMessages();
    network_core->is_dispatching_callbacks = true;
    Status<void> dispatch_status = network_core->DispatchCallbacks(*this);
    network_core->is_dispatching_callbacks = false;
    return dispatch_status;
}

Server* NetworkManager::GetServer(EndpointId id)
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    if (network_core != nullptr && network_core->owner_thread_id != std::thread::id { } && !IsOwnerThread())
    {
        assert(false && "[NetworkManager] GetServer must be called from the owner thread.");
        return nullptr;
    }

    return network_core != nullptr ? network_core->FindServer(id) : nullptr;
}

Client* NetworkManager::GetClient(EndpointId id)
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    if (network_core != nullptr && network_core->owner_thread_id != std::thread::id { } && !IsOwnerThread())
    {
        assert(false && "[NetworkManager] GetClient must be called from the owner thread.");
        return nullptr;
    }

    return network_core != nullptr ? network_core->FindClient(id) : nullptr;
}

Status<void> NetworkManager::ValidateConfig(const NetworkConfig& network_config) const
{
    if (network_config.backend == BackendType::None)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Backend type is required.");
    }

    if (network_config.backend == BackendType::IoUring)
    {
        return NetworkError(NetworkStatus::Unsupported, "[NetworkManager] io_uring backend is not supported in the first version.");
    }

    if (network_config.worker_count == 0)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Worker count must be greater than zero.");
    }

    if (network_config.servers.empty() && network_config.clients.empty())
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] At least one endpoint is required.");
    }

    if (!network_config.owned_objects.IsComplete())
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Codec registry, allocator, and packet pipeline are required.");
    }

    Status<void> registry_status = network_config.owned_objects.codec_registry->Validate();
    if (registry_status.IsFailed())
    {
        return registry_status;
    }

    for (const ServerConfig& server_config : network_config.servers)
    {
        Status<void> status = ValidateServerConfig(server_config, network_config);
        if (status.IsFailed())
        {
            return status;
        }
    }

    for (const ClientConfig& client_config : network_config.clients)
    {
        Status<void> status = ValidateClientConfig(client_config, network_config);
        if (status.IsFailed())
        {
            return status;
        }
    }

    for (size_t i = 0; i < network_config.servers.size(); ++i)
    {
        for (size_t j = i + 1; j < network_config.servers.size(); ++j)
        {
            if (network_config.servers[i].id == network_config.servers[j].id)
            {
                return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Server endpoint ids must be unique.");
            }
        }

        for (const ClientConfig& client_config : network_config.clients)
        {
            if (network_config.servers[i].id == client_config.id)
            {
                return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Endpoint ids must be unique across servers and clients.");
            }
        }
    }

    for (size_t i = 0; i < network_config.clients.size(); ++i)
    {
        for (size_t j = i + 1; j < network_config.clients.size(); ++j)
        {
            if (network_config.clients[i].id == network_config.clients[j].id)
            {
                return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Client endpoint ids must be unique.");
            }
        }
    }

    return Status<void>::Success();
}

Status<void> NetworkManager::ValidateServerConfig(const ServerConfig& server_config, const NetworkConfig& network_config) const
{
    if (server_config.id == INVALID_ENDPOINT_ID)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Server endpoint id is required.");
    }

    if (server_config.transport != TransportProtocol::Tcp)
    {
        return NetworkError(NetworkStatus::Unsupported, "[NetworkManager] Only TCP server endpoints are supported in the first version.");
    }

    if (server_config.bind.host.empty() || server_config.bind.port == 0)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Server bind address and port are required.");
    }

    if (server_config.worker_id >= network_config.worker_count)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Server worker id is out of range.");
    }

    if (server_config.max_connections == 0
        || server_config.send_queue_capacity_per_connection == 0
        || server_config.receive_queue_capacity_per_connection == 0
        || server_config.delivery_queue_capacity == 0
        || server_config.codec_buffer_capacity == 0)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Server capacities must be greater than zero.");
    }

    if (server_config.codec_buffer_capacity > MAX_PACKET_PAYLOAD_SIZE)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Server codec buffer capacity exceeds the packet payload limit.");
    }

    return Status<void>::Success();
}

Status<void> NetworkManager::ValidateClientConfig(const ClientConfig& client_config, const NetworkConfig& network_config) const
{
    if (client_config.id == INVALID_ENDPOINT_ID)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Client endpoint id is required.");
    }

    if (client_config.transport != TransportProtocol::Tcp)
    {
        return NetworkError(NetworkStatus::Unsupported, "[NetworkManager] Only TCP client endpoints are supported in the first version.");
    }

    if (client_config.remote.host.empty() || client_config.remote.port == 0)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Client remote address and port are required.");
    }

    if (client_config.worker_id >= network_config.worker_count)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Client worker id is out of range.");
    }

    if (client_config.send_queue_capacity == 0
        || client_config.receive_queue_capacity == 0
        || client_config.delivery_queue_capacity == 0
        || client_config.codec_buffer_capacity == 0)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Client capacities must be greater than zero.");
    }

    if (client_config.codec_buffer_capacity > MAX_PACKET_PAYLOAD_SIZE)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Client codec buffer capacity exceeds the packet payload limit.");
    }

    return Status<void>::Success();
}

Status<void> NetworkManager::CheckOwnerThread(const char* context) const
{
    (void)context;

    if (!IsOwnerThread())
    {
        assert(false && "[NetworkManager] Public API must be called from the owner thread.");
        return NetworkError(NetworkStatus::InvalidState, "[NetworkManager] Public API must be called from the owner thread.");
    }

    return Status<void>::Success();
}

void NetworkManager::BindOwnerThread() noexcept
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    if (network_core->owner_thread_id == std::thread::id { })
    {
        network_core->owner_thread_id = std::this_thread::get_id();
    }
}

bool NetworkManager::IsOwnerThread() const noexcept
{
    const NetworkCore* network_core = static_cast<const NetworkCore*>(core);
    return network_core->owner_thread_id != std::thread::id { } && network_core->owner_thread_id == std::this_thread::get_id();
}


} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
