#pragma once

#include <Hypnos-Core/Base/Platform.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

using EndpointId = uint16;
using PacketSize = uint16;

constexpr PacketSize PACKET_HEADER_SIZE = 4;
constexpr PacketSize MAX_PACKET_PAYLOAD_SIZE = 65535;
constexpr EndpointId INVALID_ENDPOINT_ID = 0;

enum class TransportProtocol : uint8
{
    Tcp,
    Udp,
    Kcp
};

enum class BackendType : uint8
{
    None,
    Epoll,
    IoUring
};

enum class NetworkStatus : uint8
{
    InvalidConfig,
    InvalidState,
    InvalidHandle,
    NotReady,
    Busy,
    TransportError,
    CodecError,
    ResourceExhausted,
    Unsupported
};

enum class ManagerState : uint8
{
    Unconfigured,
    Configured,
    Running,
    Stopping
};

enum class ServerState : uint8
{
    Stopped,
    Starting,
    Listening,
    Stopping,
    Failed
};

enum class ClientState : uint8
{
    Disconnected,
    Connecting,
    Connected,
    Disconnecting,
    Failed
};

enum class ConnectionState : uint8
{
    Connecting,
    Connected,
    Closing,
    Closed,
    Failed
};

enum class ConnectionEventType : uint8
{
    Connected,
    Disconnected,
    Closed,
    Failed
};

inline ErrorCode ToErrorCode(NetworkStatus status) noexcept
{
    switch (status)
    {
        case NetworkStatus::InvalidConfig:
            return ErrorCode::ConfigurationError;
        case NetworkStatus::InvalidState:
            return ErrorCode::InvalidArgument;
        case NetworkStatus::InvalidHandle:
            return ErrorCode::InvalidHandle;
        case NetworkStatus::NotReady:
            return ErrorCode::NotReady;
        case NetworkStatus::Busy:
            return ErrorCode::Busy;
        case NetworkStatus::TransportError:
            return ErrorCode::IOError;
        case NetworkStatus::CodecError:
            return ErrorCode::InvalidFormat;
        case NetworkStatus::ResourceExhausted:
            return ErrorCode::ResourceExhausted;
        case NetworkStatus::Unsupported:
            return ErrorCode::Unsupported;
    }
    return ErrorCode::Unknown;
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
