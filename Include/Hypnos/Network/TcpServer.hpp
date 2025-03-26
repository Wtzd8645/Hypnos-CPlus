#pragma once

#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/SocketServerBase.hpp"
#include "RequestPoolBase.hpp"
#include "ResponsePoolBase.hpp"
#include <Hypnos-Core/Cache/IndexedMmapBufferPool.hpp>
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
    TcpServer();
    ~TcpServer();

    void Initialize() override;
    void Release() override;

    void Listen() override;
    void Dispatch() override;
    void Close(Container::List<ConnectionHandle>* conn_handles) override;
    void Send(Container::List<ConnectionHandle>* conn_handles, ResponseBase* resp) override;

private:
    static constexpr int32 IO_RECV_BUF_GROUP = 0;

    Socket sock;
    int32 send_efd;
    Thread io_thread;
    alignas(64) Atomic<bool> running;
    alignas(64) Atomic<uint8> polling;

    io_uring io_ring;
    io_uring_params io_params;

    size_t buf_meta_offset;
    int32 io_recv_buf_mask;
    io_uring_buf_ring* io_recv_buf_ring;
    Cache::IndexedMmapBufferPool io_recv_buf_pool;
    Cache::SPSC::MmapBufferPool io_send_buf_pool;
    Cache::ObjectPool<socket_event_args> socket_event_args_pool;

    sockaddr_storage acpt_addr;
    socklen_t acpt_addr_len = sizeof(sockaddr_storage);

    Cache::ObjectPool<Connection> connection_pool;
    Container::SPSC::RingBuffer<ConnectionEvent> conn_events;
    Container::UnorderedMap<ConnectionEventId, EventHandlerBase<Connection*>*> conn_event_handlers;

    int32 buf_count = 0;
    Container::SPSC::RingBuffer<RequestBase*> requests;
    RequestPoolBase* request_pool;
    Container::List<EventHandlerBase<RequestBase*>*> request_handlers;

    Container::SPSC::RingBuffer<SocketOperationArgs> socket_op_args;
    ResponsePoolBase* response_pool;

    void ProcessEvents();

    void CloseInternal(Connection* conn);
    void AcceptInternal(socket_event_args* args);
    void ReceiveInternal(socket_event_args* args);
    void PollInternal(socket_event_args* args);
    void SendInternal(socket_event_args* args, const void* buf, int32 len);

    void OnCqeError(int32 err);
    void OnAccept(socket_event_args* args, int32 res, uint32 flags);
    void OnReceive(socket_event_args* args, int32 res, uint32 flags);
    void OnPoll(socket_event_args* args, int32 res, uint32 flags);
    void OnSend(socket_event_args* args, int32 res, uint32 flags);
};

} // namespace Hypnos
} // namespace Blanketmen