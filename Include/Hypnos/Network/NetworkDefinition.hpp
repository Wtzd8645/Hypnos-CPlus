#pragma once

#include <Hypnos-Core/Type.hpp>
#if defined _WIN32

#elif defined __linux__
#include <netinet/in.h>
#endif

namespace Blanketmen {
namespace Hypnos {

typedef uint8 SocketId, SocketEventId;
typedef uint16 RequestId;
typedef int16 PacketLengthSize;
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
    SocketId sockId;
    SocketEventId evtId;
};

struct PacketBuffer
{
    char_ptr data;
    int32 offset;
};

struct PacketContext
{
    char_ptr buffer;
    int32 packet_bytes = 0;
    int32 buffer_bytes = 0;
    int32 pending_bytes = sizeof(PacketLengthSize);
    int32 processed_bytes = 0;

    inline void Reset()
    {
        packet_bytes = 0;
        buffer_bytes = 0;
        pending_bytes = sizeof(PacketLengthSize);
        processed_bytes = 0;
    }
};

struct SendContext
{

};

struct Connection
{
    Socket sock = INVALID_FD;
    sockaddr_storage addr;
    socklen_t addr_len = sizeof(sockaddr_storage);
    PacketContext recv_ctx;
    PacketContext send_ctx;
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