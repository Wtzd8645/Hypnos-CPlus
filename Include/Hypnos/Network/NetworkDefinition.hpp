#pragma once

#include <Hypnos-Core/Container/List.hpp>
#include <Hypnos-Core/Container/Queue.hpp>
#include <Hypnos-Core/Types.hpp>

#if defined _WIN32

#elif defined __linux__
#include <netinet/in.h>
#endif

namespace Blanketmen {
namespace Hypnos {

constexpr const int32 MAX_ETH_MTU = 1500; // Ethernet (Standard) MTU.
constexpr const int32 MAX_WIFI_MTU = 1500; // Wi-Fi (802.11) MTU.
constexpr const int32 MAX_PPPoE_MTU = 1492; // PPPoE (DSL) MTU.
constexpr const int32 MAX_VPN_MTU = 1476; // VPN (GRE Tunnel) MTU.
constexpr const int32 MAX_JUMBO_MTU = 9000; // Jumbo Frames MTU.
constexpr const int32 MAX_LOOPBACK_MTU = 65536; // Loopback (lo Interface) MTU.
constexpr const int32 MIN_IPV6_MTU = 1280; // IPv6 (Minimum) MTU.

constexpr const int32 MAX_PACKET_SIZE = MAX_VPN_MTU;
constexpr const int32 MAX_BUFFER_SIZE = 2048;

typedef int16 packet_size;

typedef uint8 ServerId, ServerEventId;
typedef uint16 RequestId;

enum class ConnectionEventId : int8
{
    CONNECT,
    DISCONNECT
};

#if defined _WIN32
typedef SOCKET Socket;
constexpr const char* DEFAULT_PORT = "27015";
#elif defined __linux__
typedef int Socket;
constexpr const int INVALID_FD = -1;
constexpr const int SOCKET_ERROR = -1;
constexpr const uint16 DEFAULT_PORT = 27015;
#endif

enum TransportProtocol : int8
{
    LOCAL_SIMULATION = 0,
    TCP = 1,
    UDP = 2,
    RUDP = 3
};

enum socket_operation : int8
{
    ACPT,
    RECV,
    POLL,
    SEND
};

struct recv_context
{
    uint8 buffer[MAX_BUFFER_SIZE];
    packet_size packet_bytes = 0;
    packet_size waiting_bytes = sizeof(packet_size);
    packet_size received_bytes = 0;
};

struct send_context
{
    uint8* buffer = nullptr;
    packet_size pending_bytes = 0;
    packet_size processed_bytes = 0;
    Container::Queue<uint8*> pending_responses;
};

struct Connection
{
    Socket sock = INVALID_FD;
    uint8 version = 0;
    recv_context recv_ctx;
    send_context send_ctx;
};

struct ConnectionHandle
{
    Connection* conn;
    uint8 version;
    
    inline operator Connection* () { return conn; }
};

union buffer_metadata
{
    inline static buffer_metadata& get(uint8* buffer, int32 offset) { return *reinterpret_cast<buffer_metadata*>(buffer + offset); }

    struct
    {
        ConnectionHandle conn_handle;
        int32 bid;
        packet_size offest;
        packet_size size;
    } recv;
    struct
    {
        int32 conn_count;
        packet_size size;
    } send;
};

struct socket_event_args
{
    Connection* conn;
    uint8 version;
    socket_operation op;
};

struct SocketOperationArgs
{
    static const int32 MAX_PACKET_SIZE = Hypnos::MAX_PACKET_SIZE;

    Container::List<ConnectionHandle>* conn_handles;
    uint8* buffer;
    packet_size length;
};

struct ConnectionEvent
{
    ConnectionEventId evtId;
    Connection* conn;
};

struct MessageHeader
{
    uint8 seq;
    uint16 msgId;
};

} // namespace Hypnos
} // namespace Blanketmen