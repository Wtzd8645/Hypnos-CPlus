#pragma once

#include "Client.hpp"
#include "NetworkConfig.hpp"
#include "NetworkDefs.hpp"
#include "Server.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class NetworkManager
{
public:
    NetworkManager();
    ~NetworkManager();

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    NetworkManager(NetworkManager&&) = delete;
    NetworkManager& operator=(NetworkManager&&) = delete;

    Status<void> Configure(NetworkConfig&& config);
    Status<void> Start();
    Status<void> Stop();
    void Release();
    Status<void> Update();

    Server* GetServer(EndpointId id);
    Client* GetClient(EndpointId id);
    ManagerState State() const noexcept;

private:
    void* core = nullptr;

    Status<void> ValidateConfig(const NetworkConfig& config) const;
    Status<void> ValidateServerConfig(const ServerConfig& server_config, const NetworkConfig& config) const;
    Status<void> ValidateClientConfig(const ClientConfig& client_config, const NetworkConfig& config) const;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
