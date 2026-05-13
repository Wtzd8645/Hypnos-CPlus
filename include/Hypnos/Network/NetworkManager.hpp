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

#if defined(DEBUG)
#define HYP_NETWORK_MANAGER_BIND_OWNER_THREAD(manager) (manager).BindOwnerThread()
#define HYP_NETWORK_MANAGER_ASSERT_OWNER_THREAD(manager, context) (manager).AssertOwnerThread(context)
#else
#define HYP_NETWORK_MANAGER_BIND_OWNER_THREAD(manager) do { (void)(manager); } while (false)
#define HYP_NETWORK_MANAGER_ASSERT_OWNER_THREAD(manager, context) do { (void)(manager); (void)(context); } while (false)
#endif

struct CompletionArgs;
struct NetworkShard;
class Endpoint;

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
    Atomic<int32> terminal_error_code;

    NetworkConfig cfg;
    List<uint32> shard_cpu_ids;
    List<NetworkShard*> shards;
    List<List<Endpoint*>> shard_endpoints;
    List<Endpoint*> endpoints;

#if defined(DEBUG)
    std::thread::id owner_thread_id { };
    void BindOwnerThread() noexcept;
    bool IsOwnerThread() const noexcept;
    void AssertOwnerThread(const char* context) const noexcept;
#endif

    static void OnWakeCqe(int32 res, uint32 flags, CompletionArgs* args);

    void RunShardLoop(NetworkShard& shard);
    bool HandleWakeCqe(NetworkShard& shard, uint32 flags);
    void WakeShards();
    void OnCqeError(int32 err);
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
