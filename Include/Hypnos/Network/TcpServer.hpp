#pragma once

#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/SocketServerBase.hpp"
#include "RequestFactoryBase.hpp"
#include "ResponsePoolBase.hpp"
#include <Hypnos-Core/Cache/IndexedMmapBufferPool.hpp>
#include <Hypnos-Core/Cache/ObjectPool.hpp>
#include <Hypnos-Core/Cache/SPSC/BufferPool.hpp>
#include <Hypnos-Core/Cache/SPSC/MmapBufferPool.hpp>
#include <Hypnos-Core/Cache/SPSC/ObjectPool.hpp>
#include <Hypnos-Core/Container/Map.hpp>
#include <Hypnos-Core/Container/SPSC/RingBuffer.hpp>
#include <Hypnos-Core/System.hpp>
#include <liburing.h>
#include "RequestPoolBase.hpp"

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
    void Send(ResponseBase* resp) override;

private:
    static constexpr int32 IO_RECV_BUF_GROUP = 0;

    enum class socket_operation : int8
    {
        ACPT,
        RECV,
        POLL,
        SEND
    };

    struct socket_event_args
    {
        socket_operation op;
        Connection* conn;
    };

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

    Container::SPSC::RingBuffer<ConnectionEvent> conn_events;
    Container::UnorderedMap<ConnectionEventId, EventHandler<Connection*>*> conn_event_handlers;

    Cache::ObjectPool<socket_event_args> socket_event_args_pool;
    Cache::ObjectPool<Connection> connection_pool;
    
    sockaddr_storage acpt_addr;
    socklen_t acpt_addr_len = sizeof(sockaddr_storage);
    RequestFactoryBase* request_factory;
    RequestPoolBase* request_pool;
    ResponsePoolBase* response_pool;
    Container::SPSC::RingBuffer<uint8*> requests;
    Container::SPSC::RingBuffer<ResponseBase*> responses;

    void ProcessEvents();

    void Close(Connection* conn);
    void Release(Connection* conn);
    void Accept(socket_event_args* args);
    void Receive(socket_event_args* args);
    void Poll(socket_event_args* args);
    void Send(socket_event_args* args, const void* buf, int32 len);

    void OnCqeError(int err);
    void OnAccept(socket_event_args* args, int32 res, uint32 flags);
    void OnReceive(socket_event_args* args, int32 res, uint32 flags);
    void OnPoll(socket_event_args* args, int32 res, uint32 flags);
    void OnSend(socket_event_args* args, int32 res, uint32 flags);
};

} // namespace Hypnos
} // namespace Blanketmen