#pragma once

#include "SocketBase.hpp"
#include <Hypnos-Core/Cache/SparseObjectPool.hpp>
#include <Hypnos-Core/Container/List.hpp>
#include <Hypnos-Core/Container/SpscRingBuffer.hpp>
#include <Hypnos-Core/Mediation/IEventHandler.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class ServerSocketBase : public SocketBase
{
public:
    ServerSocketBase(SocketConfig& cfg, IOContext& ctx);
    virtual ~ServerSocketBase() = default;

    virtual void Close(List<ConnectionHandle>* conn_handles) = 0;
    virtual void Send(List<ConnectionHandle>* conns, Response* resp) = 0;

    void Register(int32 gid, IEventHandler<RequestBase*>* handler);
    void Unregister(int32 gid);

protected:
    SparseObjectPool<Connection> conntions;
    SpscRingBuffer<ServerSocketEvent> sock_events;
    IEventHandler<ConnectionHandle>* conn_event_handlers[ServerSocketEvent::MAX_EVENT_TYPES];

    SpscRingBuffer<RequestBase*> requests;
    RequestAllocatorBase* request_allocator;
    List<IEventHandler<RequestBase*>*> request_handlers;

    SpscRingBuffer<ResponseArgs> response_args;
    IMessageAllocator* response_allocator;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen