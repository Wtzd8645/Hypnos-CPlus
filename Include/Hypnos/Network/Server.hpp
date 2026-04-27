#pragma once

#include "ConnectionHandle.hpp"
#include "IMessage.hpp"
#include "NetworkDefs.hpp"
#include <Hypnos-Core/Mediation/Delegate.hpp>
#include <span>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class Server
{
public:
    virtual ~Server() = default;

    const uint8 id;
    const int32 max_conns = 0;

    Status<void> Close(ConnectionHandle conn_handle);
    virtual Status<void> Close(std::span<const ConnectionHandle> conn_handles) = 0;
    virtual Status<void> Broadcast(const IMessage& message) = 0;
    virtual Status<void> Send(std::span<const ConnectionHandle> conn_handles, const IMessage& message) = 0;
    Status<void> Send(ConnectionHandle conn_handle, const IMessage& message);

    void Register(ConnectionEventType type, Delegate<ConnectionEvent> handler);
    void Unregister(ConnectionEventType type);

protected:
    Server(int32 id, int32 max_conns) :
        id(static_cast<uint8>(id)),
        max_conns(max_conns),
        request_dispatcher(nullptr),
        request_allocator(nullptr)
    {
    }

    IMessageDispatcher* request_dispatcher;
    IMessageAllocator* request_allocator;
    Delegate<ConnectionEvent> conn_event_handlers[CONNECTION_EVENT_TYPE_COUNT];
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
