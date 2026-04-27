#pragma once

#include "NetworkDefs.hpp"
#include <cstddef>
#include <limits>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct Connection;
class TcpServer;

enum class ConnectionEventType : uint8
{
    Connected,
    Disconnected
};

constexpr size_t CONNECTION_EVENT_TYPE_COUNT = 2;

inline size_t ToIndex(ConnectionEventType type) noexcept
{
    return static_cast<size_t>(type);
}

struct ConnectionHandle
{
    ConnectionHandle() = default;

    bool IsValid() const noexcept
    {
        return endpoint_id != std::numeric_limits<uint16>::max() &&
               shard_id != std::numeric_limits<uint16>::max() &&
               slot != std::numeric_limits<uint16>::max() &&
               generation != 0;
    }

    explicit operator bool() const noexcept
    {
        return IsValid();
    }

private:
    friend struct Connection;
    friend class TcpServer;
    friend bool operator==(const ConnectionHandle& lhs, const ConnectionHandle& rhs) noexcept;

    ConnectionHandle(uint16 endpoint_id, uint16 shard_id, uint16 slot, uint32 generation) :
        endpoint_id(endpoint_id),
        shard_id(shard_id),
        slot(slot),
        generation(generation)
    {
    }

    uint16 endpoint_id = std::numeric_limits<uint16>::max();
    uint16 shard_id = std::numeric_limits<uint16>::max();
    uint16 slot = std::numeric_limits<uint16>::max();
    uint32 generation = 0;
};

inline bool operator==(const ConnectionHandle& lhs, const ConnectionHandle& rhs) noexcept
{
    return lhs.endpoint_id == rhs.endpoint_id &&
           lhs.shard_id == rhs.shard_id &&
           lhs.slot == rhs.slot &&
           lhs.generation == rhs.generation;
}

inline bool operator!=(const ConnectionHandle& lhs, const ConnectionHandle& rhs) noexcept
{
    return !(lhs == rhs);
}

struct ConnectionEvent
{
    ConnectionEventType type = ConnectionEventType::Connected;
    ConnectionHandle handle;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
