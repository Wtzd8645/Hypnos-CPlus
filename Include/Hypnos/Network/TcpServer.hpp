#pragma once

#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/SocketServerBase.hpp"
#include <Hypnos-Core/Container/UnorderedMap.hpp>
#include <Hypnos-Core/Threads/SPSC/RingBuffer.hpp>
#include <Hypnos-Core/Thread.hpp>
#include <atomic>
#if defined _WIN32

#elif defined __linux__
#include <liburing.h>
#endif
#include "RequestFactoryBase.hpp"

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

    std::atomic<bool> running;
    std::unique_ptr<Thread> io_thread;
    io_uring io_ring;

    Threads::SPSC::RingBuffer<ConnectionEvent> events;
    Container::UnorderedMap<SocketId, EventHandler<ConnectionEvent>*> event_handlers;

    Connection accept_conn;
    Container::UnorderedMap<Socket, Connection*> connection_map;

    RequestFactoryBase* request_factory;
    Threads::SPSC::RingBuffer<RequestBase*> requests;

    void Accept();
    void Close(Connection* conn);
    void Receive(Connection* conn);

    void ProcessEvents();
    void OnCqeError(int err);

    void OnAccept(Connection* conn, int32 res, int32 flags);
    void OnAcceptSuccess(Connection* conn, int32 res);
    void OnAcceptError(Connection* conn, int32 err);

    void OnReceive(Connection* conn, int32 res, int32 flags);
    void OnReceiveSuccess(Connection* conn, int32 res);
    void OnReceiveError(Connection* conn, int32 err);

    void OnSend(Connection* conn, int32 res);
    void OnSendSuccess(Connection* conn, int32 res);
    void OnSendError(Connection* conn, int32 err);
};

} // namespace Hypnos
} // namespace Blanketmen