#pragma once

#include "NetworkDefs.hpp"
#include <Hypnos-Kernel/Core/Container.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct RecvContext
{
    uint8 buffer[MAX_BUFFER_SIZE];
    packet_size packet_bytes = 0;
    packet_size waiting_bytes = sizeof(packet_size);
    packet_size received_bytes = 0;
};

struct SendContext
{
    uint8* buffer = nullptr;
    packet_size pending_bytes = 0;
    packet_size processed_bytes = 0;
    Container::Queue<uint8*> pending_responses;
};

struct Connection
{
    int32 sock_fd = INVALID_FD;
    uint8 version = 0;
    RecvContext recv_ctx;
    SendContext send_ctx;
};

struct ConnectionHandle
{
    Connection* conn;
    uint8 version;

    inline ConnectionHandle() : conn(nullptr), version(0) { }
    inline ConnectionHandle(Connection* conn) : conn(conn), version(conn->version) { }
    inline operator Connection* () { return conn; }
};

struct ConnectionEventArgs
{
    Container::List<ConnectionHandle>* conn_handles;
    uint8* buffer;
    packet_size length;
};

struct ConnectionEvent
{
    enum class Type : uint8
    {
        Connect,
        Disconnect
    };

    static constexpr size_t MAX_EVENT_TYPES = static_cast<size_t>(Type::Disconnect) + 1;

    Type type;
    ConnectionHandle conn_handle;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen