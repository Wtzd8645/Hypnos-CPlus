#pragma once

#include "NetworkDefs.hpp"
#include <Hypnos-Core/Container/Queue.hpp>
#include <limits>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct RecvContext
{
    packet_size received_bytes = 0;
    TransportHeader header = { };
    byte* buffer = nullptr;
};

struct SendContext
{
    packet_size pending_bytes = 0;
    packet_size processed_bytes = 0;
    Queue<byte*> pending_responses;
    byte* buffer = nullptr;
};

struct Connection
{
    int32 sock_fd = INVALID_FD;
    uint16 rid = 0;
    uint16 slot = 0;
    uint8 version = 0;
    RecvContext recv_ctx;
    SendContext send_ctx;
};

// Opaque transport peer handle. TCP maps this to a live connection slot; future transports may map it to a tracked peer/session.
struct ConnectionHandle
{
    uint16 rid = std::numeric_limits<uint16>::max();
    uint16 slot = std::numeric_limits<uint16>::max();
    uint8 version = 0;

    ConnectionHandle() = default;
    explicit ConnectionHandle(const Connection& conn) : rid(conn.rid), slot(conn.slot), version(conn.version) { }

    bool IsValid() const noexcept
    {
        return rid != std::numeric_limits<uint16>::max() && slot != std::numeric_limits<uint16>::max();
    }
};

inline bool operator==(const ConnectionHandle& lhs, const ConnectionHandle& rhs) noexcept
{
    return lhs.rid == rhs.rid && lhs.slot == rhs.slot && lhs.version == rhs.version;
}

// Transport peer lifecycle events. Future datagram transports may map these to peer observed/peer evicted semantics.
struct ConnectionEvent
{
    enum class Type : uint8
    {
        None,
        Connected,
        Disconnected,

        Count
    };

    Type type;
    ConnectionHandle conn_handle;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
