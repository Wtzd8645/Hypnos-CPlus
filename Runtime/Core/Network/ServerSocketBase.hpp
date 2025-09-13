#pragma once

#include "SocketBase.hpp"
#include <Hypnos-Kernel/Cache/TrackedObjectPool.hpp>
#include <Hypnos-Kernel/Container/List.hpp>
#include <Hypnos-Kernel/Container/SpscRingBuffer.hpp>
#include <Hypnos-Kernel/Mediation/EventHandlerBase.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class ServerSocketBase : public SocketBase
{
public:
    ServerSocketBase(SocketConfig& cfg, IOContext& ctx);
    virtual ~ServerSocketBase() = default;

    virtual void Close(List<ConnectionHandle>* conn_handles) = 0;
    virtual void Send(List<ConnectionHandle>* conns, ResponseBase* resp) = 0;

    void Register(int32 gid, EventHandlerBase<RequestBase*>* handler);
    void Unregister(int32 gid);

protected:
    TrackedObjectPool<Connection> conntions;
    SpscRingBuffer<ServerSocketEvent> sock_events;
    EventHandlerBase<ConnectionHandle>* conn_event_handlers[ServerSocketEvent::MAX_EVENT_TYPES];

    SpscRingBuffer<RequestBase*> requests;
    RequestAllocatorBase* request_allocator;
    List<EventHandlerBase<RequestBase*>*> request_handlers;

    SpscRingBuffer<ResponseArgs> response_args;
    ResponseAllocatorBase* response_allocator;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen