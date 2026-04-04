#pragma once

#include "ClientBase.hpp"
#include "EndpointBase.hpp"
#include "IOContext.hpp"
#include "NetworkConfig.hpp"
#include "NetworkDefs.hpp"
#include "ServerBase.hpp"
#include <Hypnos-Core/Container/List.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class NetworkManager
{
public:
    NetworkManager() : running(false), terminal_error_code(0), io_thread(nullptr), io_ctx(nullptr) { }
    ~NetworkManager() { Release(); }

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    NetworkManager(NetworkManager&&) = delete;
    NetworkManager& operator=(NetworkManager&&) = delete;

    Status<void> Configure(const NetworkConfig& cfg);
    void Release();
    Status<void> Start();
    void Stop();
    Status<void> Update();

    ServerBase* GetServer(uint8 id) const;
    ClientBase* GetClient(uint8 id) const;

private:
    alignas(CACHE_LINE_SIZE) Atomic<bool> running;
    Atomic<int32> terminal_error_code;

    NetworkConfig cfg;
    Thread* io_thread;
    IOContext* io_ctx;
    List<EndpointBase*> endpoints;

    void Process(uint32 cpu_id);
    void OnCqeError(int32 err);
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
