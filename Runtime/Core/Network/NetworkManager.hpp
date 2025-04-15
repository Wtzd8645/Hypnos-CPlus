#pragma once

#include "IOUringContext.hpp"
#include "NetworkConfig.hpp"
#include "NetworkDefs.hpp"
#include "ServerSocketBase.hpp"
#include <liburing.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class NetworkManager
{
public:
    inline static NetworkManager& Instance() noexcept
    {
        static NetworkManager instance;
        return instance;
    }

    inline static void SetConfig(const NetworkConfig& cfg) noexcept
    {
        NetworkManager::config = cfg;
    }

private:
    static NetworkConfig config;

    NetworkManager() = default;
    ~NetworkManager() = default;

    NetworkManager(NetworkManager const&) = delete;
    NetworkManager& operator=(NetworkManager const&) = delete;

public:
    void Initialize();
    void Release();

    inline void Update()
    {
        for (auto& sock : sockets)
        {
            sock->Dispatch();
        }
    }

private:
    alignas(CACHE_LINE_SIZE) Atomic<bool> running;

    Thread io_thread;
    IOUringContext* io_ctx;

    Container::List<SocketBase*> sockets;
    Container::List<ServerSocketBase*> servers;

    void ProcessIOEvents();
    void OnCqeError(int32 err);
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen