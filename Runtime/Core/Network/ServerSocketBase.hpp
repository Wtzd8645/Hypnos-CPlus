#pragma once

#include "SocketBase.hpp"
#include <Hypnos-Kernel/Core/Cache.hpp>
#include <Hypnos-Kernel/Core/Container.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class ServerSocketBase : public SocketBase
{
public:
    ServerSocketBase(SocketConfig& cfg, IOUringContext& ctx);
    virtual ~ServerSocketBase() = default;

    virtual void Close(Container::List<ConnectionHandle>* conn_handles) = 0;
    virtual void Send(Container::List<ConnectionHandle>* conns, ResponseBase* resp) = 0;

    void Register(int32 gid, EventHandlerBase<RequestBase*>* handler);
    void Unregister(int32 gid);

protected:
    Cache::IndexedObjectPool<Connection> conn_pool;
    Container::SPSC::RingBuffer<ServerSocketEvent> sock_events;
    EventHandlerBase<ConnectionHandle>* conn_event_handlers[ServerSocketEvent::MAX_EVENT_TYPES];

    Container::SPSC::RingBuffer<RequestBase*> requests;
    RequestAllocatorBase* request_allocator;
    Container::List<EventHandlerBase<RequestBase*>*> request_handlers;

    Container::SPSC::RingBuffer<ResponseArgs> response_args;
    ResponseAllocatorBase* response_allocator;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen