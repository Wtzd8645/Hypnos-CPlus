#pragma once

#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/ConnectionPool.hpp"
#include "Hypnos/Network/SocketServerBase.hpp"
#include "RequestPoolBase.hpp"
#include "ResponsePoolBase.hpp"
#include <Hypnos-Core/Cache/ObjectPool.hpp>
#include <Hypnos-Core/Cache/SPSC/BufferPool.hpp>
#include <Hypnos-Core/Cache/SPSC/MmapBufferPool.hpp>
#include <Hypnos-Core/Cache/SPSC/ObjectPool.hpp>
#include <Hypnos-Core/Container/ForwardList.hpp>
#include <Hypnos-Core/Container/Map.hpp>
#include <Hypnos-Core/Container/SPSC/RingBuffer.hpp>
#include <Hypnos-Core/Mediation/EventHandlerBase.hpp>
#include <Hypnos-Core/System.hpp>
#include <liburing.h>

namespace Blanketmen {
namespace Hypnos {

class TcpServer : public SocketServerBase
{
public:
    TcpServer(io_uring_context& ctx, size_t max_conns);
    ~TcpServer();

    void Start() override;
    void Stop() override;

    void Dispatch() override;
    void ProcessEvent(io_event_args* args, int32 res, uint32 flags) override;

    void Close(Container::List<ConnectionHandle>* conn_handles) override;
    void Send(Container::List<ConnectionHandle>* conn_handles, ResponseBase* resp) override;

private:
    static constexpr int32 IO_RECV_BUF_GROUP = 0;

    int32 sock_fd;
    alignas(64) Atomic<uint8> polling;

    ConnectionPool connection_pool;
    Container::SPSC::RingBuffer<ConnectionEvent> conn_events;
    Container::UnorderedMap<ConnectionEventId, EventHandlerBase<Connection*>*> conn_event_handlers;

    Container::SPSC::RingBuffer<RequestBase*> requests;
    RequestPoolBase* request_pool;
    Container::List<EventHandlerBase<RequestBase*>*> request_handlers;

    Container::SPSC::RingBuffer<SocketOperationArgs> socket_op_args;
    ResponsePoolBase* response_pool;


    void ProcessEvents();

    void CloseInternal(Connection* conn);
    void AcceptInternal(io_event_args* args);
    void ReceiveInternal(io_event_args* args);
    void PollInternal(io_event_args* args);
    void SendInternal(io_event_args* args, const void* buf, int32 len);

    void OnCqeError(int32 err);
    void OnAccept(io_event_args* args, int32 res, uint32 flags);
    void OnReceive(io_event_args* args, int32 res, uint32 flags);
    void OnPoll(io_event_args* args, int32 res, uint32 flags);
    void OnSend(io_event_args* args, int32 res, uint32 flags);
};

} // namespace Hypnos
} // namespace Blanketmen