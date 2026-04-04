#pragma once

namespace Blanketmen {
namespace Hypnos {
namespace Network {

constexpr uint32 MAX_ETH_MTU = 1500;  // Ethernet (Standard) MTU.
constexpr uint32 MAX_WIFI_MTU = 1500;  // Wi-Fi (802.11) MTU.
constexpr uint32 MAX_PPPoE_MTU = 1492;  // PPPoE (DSL) MTU.
constexpr uint32 MAX_VPN_MTU = 1476;  // VPN (GRE Tunnel) MTU.
constexpr uint32 MAX_JUMBO_MTU = 9000;  // Jumbo Frames MTU.
constexpr uint32 MAX_LOOPBACK_MTU = 65536;  // Loopback (lo Interface) MTU.
constexpr uint32 MIN_IPV6_MTU = 1280;  // IPv6 (Minimum) MTU.
constexpr uint32 MAX_PACKET_SIZE = MAX_VPN_MTU;
constexpr uint32 MAX_BUFFER_SIZE = 2048;

constexpr int32 INVALID_FD = -1;
constexpr int32 SOCKET_ERROR = -1;
constexpr uint16 DEFAULT_PORT = 27015;
constexpr uint8 PACKET_CODEC_NONE = 0;

typedef uint16 packet_size;

enum class TransportProtocol : uint8
{
    Mock = 0,
    TCP = 1,
    UDP = 2,
    KCP = 3
};

struct TransportHeader
{
    uint16 length;
    uint8 codec;
    uint8 flags;
};

struct ProtocolHeader
{
    uint8 seq;
    uint8 flags;
    uint16 msg_id;
    uint64 token;
};

constexpr packet_size TRANSPORT_HEADER_SIZE = sizeof(TransportHeader);

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
