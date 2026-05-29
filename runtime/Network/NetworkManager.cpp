#include <Hypnos/Network/NetworkManager.hpp>

#include "NetworkCore.hpp"

#include <cassert>
#include <memory>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

static Status<void> ValidateDenseEndpointIds(const NetworkConfig& network_config)
{
    size_t endpoint_count = network_config.servers.size() + network_config.clients.size();
    if (endpoint_count > INVALID_ENDPOINT_ID)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Endpoint count exceeds the dense endpoint id range.");
    }

    List<bool> seen_endpoint_ids;
    seen_endpoint_ids.resize(endpoint_count);

    for (const ServerConfig& server_config : network_config.servers)
    {
        if (server_config.id == INVALID_ENDPOINT_ID || static_cast<size_t>(server_config.id) >= endpoint_count)
        {
            return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Server endpoint id must be dense from zero.");
        }

        if (seen_endpoint_ids[server_config.id])
        {
            return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Endpoint ids must be unique.");
        }

        seen_endpoint_ids[server_config.id] = true;
    }

    for (const ClientConfig& client_config : network_config.clients)
    {
        if (client_config.id == INVALID_ENDPOINT_ID || static_cast<size_t>(client_config.id) >= endpoint_count)
        {
            return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Client endpoint id must be dense from zero.");
        }

        if (seen_endpoint_ids[client_config.id])
        {
            return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Endpoint ids must be unique.");
        }

        seen_endpoint_ids[client_config.id] = true;
    }

    for (bool is_seen : seen_endpoint_ids)
    {
        if (!is_seen)
        {
            return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Endpoint ids must be contiguous from zero.");
        }
    }

    return Status<void>::Success();
}

static Status<void> ValidateCodecTable(const NetworkConfig& network_config)
{
    if (network_config.message_allocator == nullptr || network_config.packet_pipeline == nullptr || network_config.codecs.empty())
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Codecs, allocator, and packet pipeline are required.");
    }

    if (network_config.codecs.size() > 256)
    {
        return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Codec table exceeds the packet header codec id range.");
    }

    for (size_t i = 0; i < network_config.codecs.size(); ++i)
    {
        if (network_config.codecs[i] == nullptr)
        {
            return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Codec table entries must not be null.");
        }

        if (network_config.codecs[i]->Id() != static_cast<uint8>(i))
        {
            return NetworkError(NetworkStatus::InvalidConfig, "[NetworkManager] Codec ids must be dense and match their table index.");
        }
    }

    return Status<void>::Success();
}

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
    return network_core != nullptr ? network_core->manager_state : ManagerState::Unconfigured;
}

Status<void> NetworkManager::Configure(NetworkConfig&& network_config)
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
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
    network_core->delivered_messages.clear();

    size_t delivery_capacity = 0;
    size_t endpoint_count = network_core->config.servers.size() + network_core->config.clients.size();
    network_core->endpoints.resize(endpoint_count);

    network_core->servers.resize(network_core->config.servers.size());
    for (size_t i = 0; i < network_core->config.servers.size(); ++i)
    {
        const ServerConfig& server_config = network_core->config.servers[i];
        Server& server = network_core->servers[i];
        server.endpoint = nullptr;

        auto endpoint = std::make_unique<Endpoint>();
        endpoint->InitializeServer(*network_core, server, server_config);
        server.endpoint = endpoint.get();
        network_core->endpoints[server_config.id] = std::move(endpoint);
        delivery_capacity += server_config.delivery_queue_capacity;
    }

    network_core->clients.resize(network_core->config.clients.size());
    for (size_t i = 0; i < network_core->config.clients.size(); ++i)
    {
        const ClientConfig& client_config = network_core->config.clients[i];
        Client& client = network_core->clients[i];
        client.endpoint = nullptr;

        auto endpoint = std::make_unique<Endpoint>();
        endpoint->InitializeClient(*network_core, client, client_config);
        client.endpoint = endpoint.get();
        network_core->endpoints[client_config.id] = std::move(endpoint);
        delivery_capacity += client_config.delivery_queue_capacity;
    }

    network_core->delivered_messages.reserve(delivery_capacity);
    network_core->manager_state = ManagerState::Configured;
    return Status<void>::Success();
}

Status<void> NetworkManager::Start()
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
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
    network_core->Stop();
    network_core->manager_state = ManagerState::Configured;
    return Status<void>::Success();
}

void NetworkManager::Release()
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    if (network_core != nullptr)
    {
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
    }
}

Status<void> NetworkManager::Update()
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
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
    network_core->ProcessDeliveries();
    network_core->is_dispatching_callbacks = false;
    return Status<void>::Success();
}

Server* NetworkManager::GetServer(EndpointId id)
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
    return network_core != nullptr ? network_core->FindServer(id) : nullptr;
}

Client* NetworkManager::GetClient(EndpointId id)
{
    NetworkCore* network_core = static_cast<NetworkCore*>(core);
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

    Status<void> codec_status = ValidateCodecTable(network_config);
    if (codec_status.IsFailed())
    {
        return codec_status;
    }

    Status<void> endpoint_id_status = ValidateDenseEndpointIds(network_config);
    if (endpoint_id_status.IsFailed())
    {
        return endpoint_id_status;
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

    return Status<void>::Success();
}

Status<void> NetworkManager::ValidateServerConfig(const ServerConfig& server_config, const NetworkConfig& network_config) const
{
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

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
