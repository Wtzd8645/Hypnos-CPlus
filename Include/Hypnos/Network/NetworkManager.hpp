#pragma once

#include "Client.hpp"
#include "NetworkConfig.hpp"
#include "NetworkDefs.hpp"
#include "Server.hpp"
#include <Hypnos-Core/Container/List.hpp>
#include <thread>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct CompletionArgs;
struct NetworkShard;
class EndpointRuntime;

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

    Server* GetServer(uint8 id) const;
    Client* GetClient(uint8 id) const;

private:
    alignas(CACHE_LINE_SIZE) Atomic<bool> running;
    std::thread::id owner_thread_id { };
    Atomic<int32> terminal_error_code;

    NetworkConfig cfg;
    List<uint32> shard_cpu_ids;
    List<NetworkShard*> shards;
    List<List<EndpointRuntime*>> shard_endpoints;
    List<EndpointRuntime*> endpoints;

    static void OnWakeCqe(int32 res, uint32 flags, CompletionArgs* args);
    void RunShardLoop(NetworkShard& shard);
    bool HandleWakeCqe(NetworkShard& shard, uint32 flags);
    void WakeShards();
    void OnCqeError(int32 err);
    bool ClaimOwnerThread() noexcept;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
