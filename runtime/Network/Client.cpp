#include <Hypnos/Network/Client.hpp>

#include "NetworkCore.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

static Status<void> NotReady()
{
    return Status<void>::Error(ToErrorCode(NetworkStatus::NotReady), "[Client] Endpoint is not bound.");
}

static Endpoint* ResolveEndpoint(void* endpoint) noexcept
{
    return static_cast<Endpoint*>(endpoint);
}

Client::Client(void* endpoint) :
    endpoint(endpoint)
{
}

Status<void> Client::Register(MessageHandler handler)
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Register(handler);
}

Status<void> Client::Register(ConnectionEventHandler handler)
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Register(handler);
}

Status<void> Client::Register(ErrorHandler handler)
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Register(handler);
}

Status<void> Client::Connect()
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Connect();
}

Status<void> Client::Disconnect()
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Disconnect();
}

Status<void> Client::Send(IMessage& message)
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint == nullptr
        ? NotReady()
        : endpoint->Send(message);
}

ClientState Client::State() const noexcept
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint != nullptr && endpoint->kind == EndpointKind::Client
        ? endpoint->client_state
        : ClientState::Disconnected;
}

EndpointId Client::Id() const noexcept
{
    Endpoint* endpoint = ResolveEndpoint(this->endpoint);
    return endpoint != nullptr && endpoint->kind == EndpointKind::Client
        ? endpoint->id
        : INVALID_ENDPOINT_ID;
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
