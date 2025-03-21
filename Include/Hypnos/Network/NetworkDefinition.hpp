#pragma once

#include <Hypnos-Core/Container/Queue.hpp>
#include <Hypnos-Core/Types.hpp>
#include <list>

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

typedef int16 packet_size;

typedef uint8 ServerId, ServerEventId;
typedef uint16 RequestId;

constexpr const int32 MAX_PACKET_SIZE = MAX_VPN_MTU;

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

enum TransportProtocol
{
    LocalSimulation = 0,
    TCP = 1,
    UDP = 2,
    RUDP = 3
};

struct PacketContext
{
    uint8* buffer;
    packet_size pending_bytes = 0;
    packet_size processed_bytes = 0;
};

struct Connection
{
    Socket sock = INVALID_FD;
    bool recving = false;
    bool sending = false;
    PacketContext recv_ctx;
    PacketContext send_ctx;
    Container::Queue<uint8*> pending_responses;
};

struct ReceiveMetadata
{
    inline static ReceiveMetadata& Get(uint8* buffer, int32 offset) { return *reinterpret_cast<ReceiveMetadata*>(buffer + offset); }

    Connection* conn;
    int32 bid;
    packet_size size;
};

struct SendMetadata
{
    inline static SendMetadata& Get(uint8* buffer, int32 offset) { return *reinterpret_cast<SendMetadata*>(buffer + offset); }

    int32 conn_count;
    packet_size size;
};

union BufferMetadata
{
    ReceiveMetadata recv_meta;
    SendMetadata send_meta;
};

struct ConnectionEvent
{
    ConnectionEventId evtId;
    Connection* conn;
};

struct ConnectionEventIdHash
{
    std::size_t operator()(const ConnectionEventId& id) const { return static_cast<std::size_t>(id); }
};

struct MessageHeader
{
    uint8 seq;
    uint16 msgId;
};

} // namespace Hypnos
} // namespace Blanketmen