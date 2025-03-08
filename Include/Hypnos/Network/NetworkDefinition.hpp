#pragma once

#include <Hypnos-Core/Container/Queue.hpp>
#include <Hypnos-Core/Type.hpp>
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

struct ConnectionEvent
{
    ServerId sockId;
    ServerEventId evtId;
};

struct PacketContext
{
    uint8* buffer;
    packet_size pending_bytes = 0;
    packet_size processed_bytes = 0;
    Container::Queue<uint8*> next_buffers;
};

struct Connection
{
    Socket sock = INVALID_FD;
    sockaddr_storage addr;
    socklen_t addr_len = sizeof(sockaddr_storage);
    PacketContext recv_ctx;
    PacketContext send_ctx;
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

    int32 ref_count;
    packet_size size;
};

union BufferMetadata
{
    ReceiveMetadata recv_meta;
    SendMetadata send_meta;
};

struct MessageHeader
{
    uint8 seq;
    uint16 msgId;
};

} // namespace Hypnos
} // namespace Blanketmen

template<>
struct std::hash<Blanketmen::Hypnos::Connection>
{
    std::size_t operator()(Blanketmen::Hypnos::Connection const& conn) const noexcept
    {
        return std::hash<Blanketmen::Hypnos::Socket>{ }(conn.sock);
    }
};