#pragma once

#include "Connection.hpp"
#include "EndpointBase.hpp"
#include "IMessage.hpp"
#include "IOContext.hpp"
#include "NetworkDefs.hpp"
#include <Hypnos-Core/Cache/SparseObjectPool.hpp>
#include <Hypnos-Core/Container/List.hpp>
#include <Hypnos-Core/Container/SpscRingBuffer.hpp>
#include <Hypnos-Core/Mediation/Delegate.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class ServerBase : public EndpointBase
{
public:
    virtual ~ServerBase() = default;

    const int32 max_conns = 0;

    virtual void Close(List<ConnectionHandle>* conn_handles) = 0;
    virtual void Broadcast(IMessage* resp) = 0;
    virtual void Send(List<ConnectionHandle>* conn_handles, IMessage* resp) = 0;

    void Register(int32 id, Delegate<ConnectionEvent> handler);
    void Unregister(int32 id);

protected:
    // TODO: Make configurable.
    ServerBase(int32 id, int32 max_conns) :
        EndpointBase(static_cast<uint8>(id), EndpointBase::Type::Server),
        max_conns(max_conns),
        request_dispatcher(nullptr),
        request_allocator(nullptr),
        response_allocator(nullptr),
        conntions(max_conns),
        conn_events(1024)
    {
    }

    IMessageDispatcher* request_dispatcher;
    IMessageAllocator* request_allocator;
    IMessageAllocator* response_allocator;

    SparseObjectPool<Connection> conntions;
    SpscRingBuffer<ConnectionEvent> conn_events;
    Delegate<ConnectionEvent> conn_event_handlers[(int)ConnectionEvent::Type::Count];
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
