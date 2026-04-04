#pragma once

#include "NetworkDefs.hpp"
#include <Hypnos-Core/Container/Queue.hpp>

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
    uint8 version = 0;
    RecvContext recv_ctx;
    SendContext send_ctx;
};

struct ConnectionHandle
{
    Connection* conn;
    uint8 version;

    ConnectionHandle() : conn(nullptr), version(0) { }
    ConnectionHandle(Connection* conn) : conn(conn), version(conn->version) { }

    operator Connection* () { return conn; }
};

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
