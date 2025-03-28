#pragma once

#include "NetworkConfig.hpp"
#include "NetworkDefinition.hpp"
#include "SocketServerBase.hpp"
#include <Hypnos-Core/Cache/SPSC/MmapBufferPool.hpp>
#include <Hypnos-Core/Container/List.hpp>
#include <Hypnos-Core/Mediation/EventHandlerBase.hpp>
#include <Hypnos-Core/System.hpp>
#include <liburing.h>

namespace Blanketmen {
namespace Hypnos {

class NetworkManager
{
public:
    inline static NetworkManager& Instance() noexcept
    {
        static NetworkManager instance;
        return instance;
    }

    inline static void SetConfig(const NetworkConfig& config) noexcept
    {
        NetworkManager::config = config;
    }

private:
    static NetworkConfig config;

    NetworkManager() = default;
    NetworkManager(NetworkManager const&) = delete;
    NetworkManager& operator=(NetworkManager const&) = delete;
    ~NetworkManager() = default;

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
    alignas(64) Atomic<bool> running;
    Thread io_thread;
    io_uring_context* io_ctx;

    Container::List<SocketBase*> sockets;
    Container::UnorderedMap<int32, SocketServerBase*> servers;

    void ProcessEvents();
    void OnCqeError(int32 err);
};

} // namespace Hypnos
} // namespace Blanketmen