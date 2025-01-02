#include "Hypnos/Logging.hpp"
#include "Hypnos/Network/NetworkUtils.hpp"
#include "Hypnos/Network/UdpListener.hpp"

namespace Blanketmen {
namespace Hypnos {

UdpListener::UdpListener(ConnectionListenerConfig& config, Container::UnorderedMap<uint16, Delegate<RequestBase&>*>& reqHandlerDict) :
    SocketListenerBase(config, reqHandlerDict),
    receiveBuf(new char[maxPacketBytes])
{
    // Create socket.
    listenSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (listenSocket == INVALID_SOCKET)
    {
        Logging::Error("[UdpListener] Create listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Bind
    NetworkUtils::GetLocalAddress(AF_INET, &acceptedSockAddr);
    ((sockaddr_in*)&acceptedSockAddr)->sin_port = ::htons(DEFAULT_PORT);
    if (::bind(listenSocket, &acceptedSockAddr, sizeof(acceptedSockAddr)) == SOCKET_ERROR)
    {
        Logging::Error("[UdpListener] Bind adderss to listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Get the socket file descriptor flags.
    int opts = ::fcntl(listenSocket, F_GETFL);
    if (opts == SOCKET_ERROR)
    {
        Logging::Error("[UdpListener] Get flags from listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Set the non-blocking flag to the socket file descriptor.
    if (::fcntl(listenSocket, F_SETFL, opts | O_NONBLOCK) == SOCKET_ERROR)
    {
        Logging::Error("[UdpListener] Set the non-blocking flag to the listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Create epoll.
    epfd = ::epoll_create(maxConnections);
    if (epfd == INVALID_FD)
    {
        Logging::Error("[UdpListener] Create epoll from the listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Register epoll event.
    epEvent.events = EPOLLIN | EPOLLET;
    epEvent.data.fd = listenSocket;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, listenSocket, &epEvent))
    {
        Logging::Error("[UdpListener] Register the listen socket epoll event failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }
    epEventBuf = new epoll_event[maxConnections];

    char ip[INET6_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &((sockaddr_in*)&acceptedSockAddr)->sin_addr, ip, INET_ADDRSTRLEN);
    Logging::Info("[UdpListener] Create the listen socket success. Bind address: %s", ip);
}

UdpListener::~UdpListener()
{
    if (receiveThread != nullptr)
    {
        receiveThread->detach();
        delete receiveThread;
    }

    if (sendThread != nullptr)
    {
        sendThread->detach();
        delete sendThread;
    }

    if (epfd != INVALID_FD)
    {
        ::close(epfd);
    }
    delete[] epEventBuf;

    if (listenSocket != INVALID_SOCKET)
    {
        ::close(listenSocket);
    }

    delete[] receiveBuf;

    if (requestProducer != nullptr)
    {
        delete requestProducer;
    }

    for (auto it = connectionMap.begin(); it != connectionMap.end(); ++it)
    {
        delete it->second;
    }
}

inline void UdpListener::Listen()
{
    receiveThread = new Thread(&UdpListener::ProcessEvents, this);
    sendThread = new Thread(&UdpListener::SendResponses, this);
    Logging::Info("[UdpListener] The server socket is listening.");
}

inline void UdpListener::Dispatch()
{
    Container::Vector<RequestBase*>* temp = consumerRequests;
    consumerRequests = producerRequests;
    requestLocker.lock();
    producerRequests = temp;
    requestLocker.unlock();

    for (auto it = consumerRequests->begin(); it != consumerRequests->end(); ++it)
    {
        Delegate<RequestBase&>* handler = requestHandlerMap[(*it)->header.msgId];
        if (handler != nullptr)
        {
            (*handler)(**it);
        }
    }
    consumerRequests->clear();
}

inline void UdpListener::Send(ResponseBase* response)
{
    responseLocker.lock();
    producerResponses->push_back(response);
    hasNewResponse = true;
    responseLocker.unlock();
    responseCv.notify_one();
}

void UdpListener::ProcessEvents()
{
    int evtNum = 0;
    SOCKET sock = 0;
    while (true)
    {
        evtNum = ::epoll_wait(epfd, epEventBuf, maxConnections, -1);
        if (evtNum < 0)
        {
            int errorCode = errno;
            if (errorCode == EINTR)
            {
                continue;
            }

            Logging::Info("[UdpListener] Wait for epoll events failed. ErrorCode: %d", errorCode);
            // TODO: Find an appropriate way to handle errors.
            continue;
        }

        while (evtNum-- > 0)
        {
            if (epEventBuf[evtNum].data.fd == listenSocket &&
                epEventBuf[evtNum].events & EPOLLIN)
            {
                Receive();
            }
        }
    }
}

inline void UdpListener::Receive()
{
    sockaddr addr;
    socklen_t addrLen = sizeof(addr);
    while (true)
    {
        ssize_t transferredBytes = ::recvfrom(listenSocket, receiveBuf, maxPacketBytes, 0, &addr, &addrLen);
        if (transferredBytes < 0)
        {
            int errorCode = errno;
            if (errorCode == EAGAIN || errorCode == EWOULDBLOCK)
            {
                break;
            }

            if (errorCode == EINTR)
            {
                continue;
            }

            Logging::Info("[UdpListener] Receive from the listen socket failed. ErrorCode: %d", errorCode);
            break;
        }

        if (transferredBytes == 0)
        {
            continue;
        }

        // TODO: Rewrite here
        // ConnectionId** connId = &connectionMap[std::move(string(addr.sa_data))];
        // if (*connId == nullptr)
        // {
        //     *connId = new ConnectionId();
        //     (*connId)->addr = addr;
        // }
        // 
        // // TODO: Optimize packet process.
        // if (!requestProducer->Produce(receiveBuf + sizeof(PacketLengthSize), *connId))
        // {
        //     Logging::Error("[UdpListener] Create resquest failed.");
        //}
    }
}

void UdpListener::SendResponses()
{
    char sendBuf[maxPacketBytes * 16];
    char compressBuf[maxPacketBytes];
    char encryptBuf[maxPacketBytes];
    PacketBuffer packetBuf{ 0, sendBuf, compressBuf, encryptBuf };
    int32 addrLen = sizeof(sockaddr);
    UniqueLock<Mutex> lock(responseLocker, std::defer_lock);

    while (true)
    {
        Container::Vector<ResponseBase*>* temp = consumerResponses;
        consumerResponses = producerResponses;
        responseCv.wait(lock, [this] { return hasNewResponse; });

        producerResponses = temp;
        hasNewResponse = false;
        responseLocker.unlock();

        for (auto respIt = consumerResponses->begin(); respIt != consumerResponses->end(); ++respIt)
        {
            ResponseBase* resp = *respIt;
            int32 packetBytes = (*respIt)->Pack(packetBuf);
            for (auto connIt = resp->connIds.begin(); connIt != resp->connIds.end(); ++connIt)
            {
                int32 sentBytes = 0;
                while (sentBytes < packetBytes) // TODO: Is UDP need to do while send?
                {
                    ssize_t result = ::sendto(listenSocket, sendBuf + sentBytes, packetBytes - sentBytes, 0, &(*connIt)->addr, addrLen);
                    if (result == SOCKET_ERROR)
                    {
                        Logging::Error("[UdpListener] Send failed. ErrorCode: %d", errno);
                        break;
                    }
                    sentBytes += result;
                }
            }
        }
        consumerResponses->clear();
    }
}

} // namespace Hypnos
} // namespace Blanketmen