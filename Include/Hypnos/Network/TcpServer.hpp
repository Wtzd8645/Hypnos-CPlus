#pragma once

#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/SocketServerBase.hpp"
#include <Hypnos-Core/Container/UnorderedMap.hpp>
#include <Hypnos-Core/Thread.hpp>
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
    void Stop() override;
    void Dispatch() override;
    void Send(ResponseBase* resp) override;

private:
    Socket server_fd;
    Connection accept_conn;

    std::atomic<bool> running{ true };
    Thread* recv_thread;
    io_uring recv_ring;
    Thread* send_thread;
    io_uring send_ring;

    Container::UnorderedMap<Socket, Connection*> connectionMap;

    void ProcessRecvEvents();
    void ProcessSendEvents();
    void Accept();
    void HandleAccept(int result);
    void Receive(Connection* conn);
    void HandleReceive(Connection* conn, int result);
    void HandleSend(Connection* conn, int result);

    void HandleCqeError(int result)
    {
        switch (-result)
        {
            case EIO:
            case EAGAIN:
            case ENOMEM:
            case EBUSY:
            {
                Logging::Warning("[TcpSocket] Failed to wait for CQE. Error: %d", result);
                usleep(1000);
                return;
            }
            case ENXIO:
            case EBADF:
            case EFAULT:
            case ENODEV:
            case EINVAL:
            case ENOSPC:
            {
                throw std::runtime_error("[TcpSocket] Fatal CQE error. Error: " + std::string(strerror(errno)));
            }
            default:
            {
                throw std::runtime_error("[TcpSocket] Unknown CQE error. Error: " + std::string(strerror(errno)));
            }
        }
    }
};

} // namespace Hypnos
} // namespace Blanketmen