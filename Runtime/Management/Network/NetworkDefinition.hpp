#ifndef NETWORK_DEFINITION_HPP_
#define NETWORK_DEFINITION_HPP_

#if defined _WIN32

#elif defined __linux__
#include <netinet/in.h>
#endif
#include <Type.hpp>

namespace Blanketmen {
namespace Hypnos {

#if defined _WIN32
typedef SOCKET SOCKET;
constexpr const char* DEFAULT_PORT = "27015";
#elif defined __linux__
typedef int SOCKET;
constexpr const int INVALID_SOCKET = -1;
constexpr const int SOCKET_ERROR = -1;
constexpr const int INVALID_FD = -1;
constexpr const uint16 DEFAULT_PORT = 27015;
#endif
typedef int16 PacketLengthSize;

enum TransportProtocol
{
    LocalSimulation = 0,
    TCP = 1,
    UDP = 2,
    RUDP = 3
};

struct ConnectionId
{
    SOCKET sock;
    sockaddr addr;
};

struct PacketBuffer
{
    int32 offset;
    char* final;
    char* compress;
    char* encrypt;
};

struct PacketReadState
{
    PacketBuffer packetBuf; // TODO: Make compress and encrypt buffer to thread static.
    bool isWaitingPacketSize = true;
    int32 waitingBytes = sizeof(PacketLengthSize);
    int32 pendingBytes = 0;
    int32 processedBytes = 0;
};

struct Connection
{
    ConnectionId connId;
    PacketReadState readState;
};

struct SerializationState
{
    char_ptr buffer;
    int32 offset;
};

struct MessageHeader
{
    uint8 seq;
    uint16 msgId;
};

} // namespace Hypnos
} // namespace Blanketmen

template<>
struct std::hash<Blanketmen::Hypnos::ConnectionId>
{
    std::size_t operator()(Blanketmen::Hypnos::ConnectionId const& connId) const noexcept
    {
        return std::hash<std::string>{ }(connId.addr.sa_data);
    }
};

#endif // NETWORK_DEFINITION_HPP_