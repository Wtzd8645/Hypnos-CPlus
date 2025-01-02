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
#include "RequestProducerBase.hpp"
#include "ResponseBase.hpp"

namespace Blanketmen {
namespace Hypnos {

class SocketListenerBase
{
public:
    SocketListenerBase(ConnectionListenerConfig& config, Container::UnorderedMap<uint16, Delegate<RequestBase&>*>& requestHandlerMap) :
        maxConnections(config.maxConnections),
        maxPacketBytes(config.maxPacketBytes),
        requestProducer(config.requestProducer),
        requestHandlerMap(requestHandlerMap)
    {
        int32 msgPoolSize = maxConnections * 4;
        producerRequests = new Container::Vector<RequestBase*>();
        producerRequests->reserve(msgPoolSize);
        consumerRequests = new Container::Vector<RequestBase*>();
        consumerRequests->reserve(msgPoolSize);

        producerResponses = new Container::Vector<ResponseBase*>();
        producerResponses->reserve(msgPoolSize);
        consumerResponses = new Container::Vector<ResponseBase*>();
        consumerResponses->reserve(msgPoolSize);
    }

    virtual ~SocketListenerBase()
    {
        delete requestProducer;
        delete producerRequests;
        delete consumerRequests;
        delete producerResponses;
        delete consumerResponses;
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
    SOCKET listenSocket = INVALID_SOCKET;

    Thread* receiveThread;
    Mutex requestLocker;
    Container::Vector<RequestBase*>* producerRequests;
    Container::Vector<RequestBase*>* consumerRequests;
    RequestProducerBase* requestProducer;
    Container::UnorderedMap<uint16, Delegate<RequestBase&>*>& requestHandlerMap;

    Thread* sendThread;
    Mutex responseLocker;
    ConditionVariable responseCv;
    bool hasNewResponse = false;
    Container::Vector<ResponseBase*>* producerResponses;
    Container::Vector<ResponseBase*>* consumerResponses;
};

} // namespace Hypnos
} // namespace Blanketmen