#pragma once

namespace Blanketmen {
namespace Hypnos {
namespace Network {

constexpr int32 MAX_ETH_MTU = 1500;  // Ethernet (Standard) MTU.
constexpr int32 MAX_WIFI_MTU = 1500;  // Wi-Fi (802.11) MTU.
constexpr int32 MAX_PPPoE_MTU = 1492;  // PPPoE (DSL) MTU.
constexpr int32 MAX_VPN_MTU = 1476;  // VPN (GRE Tunnel) MTU.
constexpr int32 MAX_JUMBO_MTU = 9000;  // Jumbo Frames MTU.
constexpr int32 MAX_LOOPBACK_MTU = 65536;  // Loopback (lo Interface) MTU.
constexpr int32 MIN_IPV6_MTU = 1280;  // IPv6 (Minimum) MTU.

constexpr int32 MAX_PACKET_SIZE = MAX_VPN_MTU;
constexpr int32 MAX_BUFFER_SIZE = 2048;
constexpr int32 IO_RECV_BUF_GROUP = 0;

constexpr int32 INVALID_FD = -1;
constexpr int32 SOCKET_ERROR = -1;
constexpr uint16 DEFAULT_PORT = 27015;

typedef int16 packet_size;

enum class EndpointRole : uint8
{
    Server,
    Client
};

enum class TransportProtocol : uint8
{
    Mock = 0,
    TCP = 1,
    UDP = 2,
    KCP = 3
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen