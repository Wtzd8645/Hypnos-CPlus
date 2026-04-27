#include <Hypnos/Network/Server.hpp>
#include <cassert>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

Status<void> Server::Close(ConnectionHandle conn_handle)
{
    const ConnectionHandle conn_handles[] = { conn_handle };
    return Close(std::span<const ConnectionHandle>(conn_handles, 1));
}

Status<void> Server::Send(ConnectionHandle conn_handle, const IMessage& message)
{
    const ConnectionHandle conn_handles[] = { conn_handle };
    return Send(std::span<const ConnectionHandle>(conn_handles, 1), message);
}

void Server::Register(ConnectionEventType type, Delegate<ConnectionEvent> handler)
{
    const size_t index = ToIndex(type);
    assert(index < CONNECTION_EVENT_TYPE_COUNT && "[Server] Invalid connection event type.");
    conn_event_handlers[index] = handler;
}

void Server::Unregister(ConnectionEventType type)
{
    const size_t index = ToIndex(type);
    assert(index < CONNECTION_EVENT_TYPE_COUNT && "[Server] Invalid connection event type.");
    conn_event_handlers[index].Unregister();
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
