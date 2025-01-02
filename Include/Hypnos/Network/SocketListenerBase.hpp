#pragma once

#if defined _WIN32
#pragma comment(lib, "Ws2_32.lib")
#include <WinSock2.h>
#include <ws2tcpip.h>
#elif defined __linux__
#include <fcntl.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#endif

#include <Hypnos-Core/Container.hpp>
#include <Hypnos-Core/Mediation.hpp>
#include <Hypnos-Core/Thread.hpp>
#include <Hypnos/Logging.hpp>
#include "NetworkDefinition.hpp"
#include "NetworkConfig.hpp"
#include "RequestFactoryBase.hpp"
#include "ResponseBase.hpp"

namespace Blanketmen {
namespace Hypnos {

class SocketListenerBase
{
public:
    SocketListenerBase(ConnectionListenerConfig& config, Container::UnorderedMap<uint16, Delegate<RequestBase&>*>& requestHandlerMap) :
        maxConnections(config.maxConnections),
        maxPacketBytes(config.maxPacketBytes),
        request_factory(config.request_factory),
        requestHandlerMap(requestHandlerMap)
    {
        int32 msgPoolSize = maxConnections * 4;
        producer_requests = new Container::Vector<RequestBase*>();
        producer_requests->reserve(msgPoolSize);
        consumer_requests = new Container::Vector<RequestBase*>();
        consumer_requests->reserve(msgPoolSize);

        producer_responses = new Container::Vector<ResponseBase*>();
        producer_responses->reserve(msgPoolSize);
        consumer_responses = new Container::Vector<ResponseBase*>();
        consumer_responses->reserve(msgPoolSize);
    }

    virtual ~SocketListenerBase()
    {
        delete request_factory;
        delete producer_requests;
        delete consumer_requests;
        delete producer_responses;
        delete consumer_responses;
    }

    virtual void Listen() = 0;
    virtual void Dispatch() = 0;
    virtual void Send(ResponseBase* response) = 0;

protected:
#if defined _WIN32
    enum IocpOperation
    {
        IOCP_RECEIVE = 0,
        IOCP_SEND = 1
    };

    struct IocpEventArgs
    {
        OVERLAPPED overlapped;
        WSABUF wsaBuffer;
        int32 operation;

        IocpEventArgs() : operation(IocpOperation::IOCP_RECEIVE)
        {
            ::memset(&overlapped, 0, sizeof(OVERLAPPED));
            wsaBuffer.len = BUFFER_LEN;
            wsaBuffer.buf = new char[BUFFER_LEN];
        }

        IocpEventArgs(ResponseBase* response) : operation(IocpOperation::IOCP_SEND)
        {
            ::memset(&overlapped, 0, sizeof(OVERLAPPED));
            wsaBuffer.buf = new char[BUFFER_LEN];
            wsaBuffer.len = response->Pack(wsaBuffer.buf);
        }
    };

    static CRITICAL_SECTION clientLocker;
    static CRITICAL_SECTION requestLocker;
    static HANDLE iocpHandle;

    SLinkedList<std::thread*> iocpThreads;
#elif defined __linux__
    int epfd;
    epoll_event epEvent;
    epoll_event* epEventBuf;
    sockaddr acceptedSockAddr;
    socklen_t acceptedSockAddrLen;
#endif
    int32 maxConnections;
    int32 maxPacketBytes;
    Socket listenSocket = INVALID_FD;

    Thread* receiveThread;
    Mutex request_mutex;
    Container::Vector<RequestBase*>* producer_requests;
    Container::Vector<RequestBase*>* consumer_requests;
    RequestFactoryBase* request_factory;
    Container::UnorderedMap<uint16, Delegate<RequestBase&>*>& requestHandlerMap;

    Thread* sendThread;
    Mutex responseLocker;
    ConditionVariable responseCv;
    bool hasNewResponse = false;
    Container::Vector<ResponseBase*>* producer_responses;
    Container::Vector<ResponseBase*>* consumer_responses;
};

} // namespace Hypnos
} // namespace Blanketmen