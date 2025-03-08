#pragma once

#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/SocketServerBase.hpp"
#include "RequestFactoryBase.hpp"
#include <Hypnos-Core/Cache/FixedMmapBufferPool.hpp>
#include <Hypnos-Core/Cache/SPSC/BufferPool.hpp>
#include <Hypnos-Core/Cache/SPSC/MmapBufferPool.hpp>
#include <Hypnos-Core/Cache/SPSC/ObjectPool.hpp>
#include <Hypnos-Core/Container/SPSC/RingBuffer.hpp>
#include <Hypnos-Core/Threads.hpp>
#include <atomic>

#if defined _WIN32

#elif defined __linux__
#include <liburing.h>
#endif

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
    Socket sock;
    std::unique_ptr<Thread> io_thread;
    std::atomic<bool> running;
    alignas(64) std::atomic<bool> processing; // PERF: Change to state.

    io_uring io_ring;
    io_uring_params io_params;
    io_uring_buf_ring* io_recv_buf_ring;

    size_t buf_meta_offset;
    int32 io_recv_buf_mask;
    Cache::FixedMmapBufferPool io_recv_buf_pool;
    Cache::SPSC::MmapBufferPool io_send_buf_pool;

    Container::SPSC::RingBuffer<ConnectionEvent> events;
    Container::UnorderedMap<ServerId, EventHandler<ConnectionEvent>*> event_handlers;

    Connection accept_conn;
    Container::UnorderedMap<Socket, Connection*> connection_map;
    
    RequestFactoryBase* request_factory;
    Container::SPSC::RingBuffer<uint8*> requests;

    int32 send_evt;
    Container::SPSC::RingBuffer<ResponseBase*> responses;

    void Accept();
    void Close(Connection* conn);
    void Receive(Connection* conn);
    void SendInternal(Connection* conn);
    void SendInternal(Connection* conn, const void* buf, int32 len);

    void ProcessEvents();
    void OnCqeError(int err);
    void OnAccept(int32 res, uint32 flags);
    void OnReceive(Connection* conn, int32 res, uint32 flags);
    void OnSend(Connection* conn, int32 res, uint32 flags);
};

} // namespace Hypnos
} // namespace Blanketmen