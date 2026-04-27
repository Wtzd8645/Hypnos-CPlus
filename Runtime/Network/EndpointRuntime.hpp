#pragma once

#include "Hypnos/Network/Client.hpp"
#include "Hypnos/Network/NetworkDefs.hpp"
#include "Hypnos/Network/Server.hpp"
#include <cassert>
#include <thread>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class EndpointRuntime
{
public:
    enum class Type : uint8
    {
        Server,
        Client
    };

    explicit EndpointRuntime(uint8 id, Type type) : id(id), type(type) { }
    virtual ~EndpointRuntime() = default;

    const uint8 id;
    const Type type;

    virtual Status<void> Start() = 0;
    virtual Status<void> Stop() = 0;
    virtual void Dispatch() = 0;
    virtual void OnShardWake(uint16 shard_id) { (void)shard_id; }
    virtual Server* AsServer() noexcept { return nullptr; }
    virtual Client* AsClient() noexcept { return nullptr; }

protected:
    inline bool ClaimOwnerThread() noexcept
    {
        const std::thread::id current_thread_id = std::this_thread::get_id();
        if (owner_thread_id == std::thread::id())
        {
            owner_thread_id = current_thread_id;
            return true;
        }

        return owner_thread_id == current_thread_id;
    }

    inline void AssertOwnerThread(const char* context) noexcept
    {
        const bool is_owner_thread = ClaimOwnerThread();
        assert(is_owner_thread && context);
        (void)is_owner_thread;
    }

private:
    Thread::id owner_thread_id { };
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
