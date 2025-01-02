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
    PacketBuffer buffer;
    bool isWaitingSize = false;
    int32 waitingBytes = sizeof(PacketLengthSize);
    int32 pendingBytes = 0;
    int32 processedBytes = 0;
};

struct Connection
{
    Socket sock = INVALID_FD;
    sockaddr_storage addr;
    socklen_t addr_len = sizeof(sockaddr_storage);
    PacketContext recvCtx;
    PacketContext sendCtx;
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