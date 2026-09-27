#include <Hypnos/Network/Server.hpp>

#include "Endpoint.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

static Status<void> NotReady()
{
    return Status<void>::Error(ToErrorCode(NetworkStatus::NotReady), "[Server] Endpoint is not bound.");
}

static Endpoint* ResolveEndpoint(void* endpoint) noexcept
{
    return static_cast<Endpoint*>(endpoint);
}

Server::Server(void* endpoint) :
    endpoint(endpoint)
{
}

Status<void> Server::Register(MessageHandler handler)
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Register(handler);
}

Status<void> Server::Register(ConnectionEventHandler handler)
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Register(handler);
}

Status<void> Server::Register(ErrorHandler handler)
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Register(handler);
}

Status<void> Server::Send(ConnectionHandle connection, IMessage& message)
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Send(connection, message);
}

Status<void> Server::Close(ConnectionHandle connection)
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Close(connection);
}

ServerState Server::State() const noexcept
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint != nullptr && endpoint->kind == EndpointKind::Server
        ? endpoint->server_state
        : ServerState::Stopped;
}

EndpointId Server::Id() const noexcept
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint != nullptr && endpoint->kind == EndpointKind::Server
        ? endpoint->id
        : INVALID_ENDPOINT_ID;
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
