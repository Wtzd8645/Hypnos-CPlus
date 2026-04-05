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
    NetworkManager() : running(false), terminal_error_code(0) { }
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
    List<uint32> reactor_cpu_ids;
    List<Thread*> io_threads;
    List<IOContext*> io_contexts;
    List<EndpointBase*> endpoints;

    static void OnWakeCqe(int32 res, uint32 flags, CompletionArgs* args);
    void Process(uint16 rid, uint32 cpu_id);
    bool HandleWakeCqe(IOContext& io_ctx, uint16 rid, uint32 flags);
    void WakeReactors();
    void OnCqeError(int32 err);
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
