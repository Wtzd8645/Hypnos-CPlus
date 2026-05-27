#pragma once

#include "ConnectionHandle.hpp"
#include "IMessage.hpp"
#include "NetworkDefs.hpp"
#include <Hypnos-Core/Mediation/Delegate.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class NetworkManager;

struct MessageEvent
{
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    ConnectionHandle connection;
    IMessage& message;
};

struct ConnectionEvent
{
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    ConnectionHandle connection;
    ConnectionEventType type = ConnectionEventType::Failed;
    ConnectionState state = ConnectionState::Failed;
};

struct NetworkErrorEvent
{
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    bool has_connection = false;
    ConnectionHandle connection;
    NetworkStatus status = NetworkStatus::TransportError;
    const char* detail = nullptr;
};

using MessageHandler = Delegate<const MessageEvent&>;
using ConnectionEventHandler = Delegate<const ConnectionEvent&>;
using ErrorHandler = Delegate<const NetworkErrorEvent&>;

class Server
{
public:
    Server() = default;

    Status<void> Register(MessageHandler handler);
    Status<void> Register(ConnectionEventHandler handler);
    Status<void> Register(ErrorHandler handler);

    Status<void> Send(ConnectionHandle connection, IMessage& message);
    Status<void> Close(ConnectionHandle connection);
    ServerState State() const noexcept;
    EndpointId Id() const noexcept;

private:
    friend class NetworkManager;

    explicit Server(void* endpoint);

    void* endpoint = nullptr;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
