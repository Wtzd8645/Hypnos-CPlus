#pragma once

#include "Hypnos/Network/Client.hpp"
#include "Hypnos/Network/NetworkDefs.hpp"
#include "Hypnos/Network/Server.hpp"
#include <cassert>
#include <thread>

#if defined(DEBUG)
#define HYP_NETWORK_ASSERT_OWNER_THREAD(context) AssertOwnerThread(context)
#else
#define HYP_NETWORK_ASSERT_OWNER_THREAD(context) do { } while (false)
#endif

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class Endpoint
{
public:
    explicit Endpoint(uint8 id) : id(id) { }
    virtual ~Endpoint() = default;

    const uint8 id;

#if defined(DEBUG)
    inline void BindOwnerThread() noexcept
    {
        assert((owner_thread_id == std::thread::id() || owner_thread_id == std::this_thread::get_id()) && "[Endpoint] Owner thread is already bound to another thread.");
        owner_thread_id = std::this_thread::get_id();
    }
#endif

    virtual Status<void> Start() = 0;
    virtual Status<void> Stop() = 0;

    virtual void Dispatch() = 0;
    virtual void HandleShardWake(uint16 shard_id) { (void)shard_id; }

    virtual Server* AsServer() noexcept { return nullptr; }
    virtual Client* AsClient() noexcept { return nullptr; }

protected:
#if defined(DEBUG)
    inline bool IsOwnerThread() const noexcept
    {
        return owner_thread_id == std::this_thread::get_id();
    }

    inline void AssertOwnerThread(const char* context) const noexcept
    {
        assert(owner_thread_id != std::thread::id() && context);
        assert(IsOwnerThread() && context);
    }
#endif

private:
#if defined(DEBUG)
    Thread::id owner_thread_id { };
#endif
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
