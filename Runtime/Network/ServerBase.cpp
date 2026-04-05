#include <Hypnos/Network/ServerBase.hpp>
#include <cassert>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

Status<void> ServerBase::Close(const ConnectionHandle& conn_handle)
{
    List<ConnectionHandle> conn_handles { conn_handle };
    return Close(conn_handles);
}

Status<void> ServerBase::Send(const ConnectionHandle& conn_handle, IMessage* resp)
{
    List<ConnectionHandle> conn_handles { conn_handle };
    return Send(conn_handles, resp);
}

void ServerBase::Register(int32 id, Delegate<ConnectionEvent> handler)
{
    assert(id >= 0 && id < (int32)ConnectionEvent::Type::Count && "[ServerBase] Invalid connection event ID.");
    conn_event_handlers[id] = handler;
}

void ServerBase::Unregister(int32 id)
{
    assert(id >= 0 && id < (int32)ConnectionEvent::Type::Count && "[ServerBase] Invalid connection event ID.");
    conn_event_handlers[id].Unregister();
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
