#include "Hypnos/Logging.hpp"
#include "Hypnos/Network/NetworkUtils.hpp"
#include "Hypnos/Network/TcpListener.hpp"

namespace Blanketmen {
namespace Hypnos {

TcpListener::TcpListener(ConnectionListenerConfig& config, Container::UnorderedMap<uint16, Delegate<RequestBase&>*>& reqestHandlerMap) :
    SocketListenerBase(config, reqestHandlerMap)
{
    // Create socket.
    listenSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET)
    {
        Logging::Error("[TcpListener] Create the listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Bind
    NetworkUtils::GetLocalAddress(AF_INET, &acceptedSockAddr);
    ((sockaddr_in*)&acceptedSockAddr)->sin_port = ::htons(DEFAULT_PORT);
    if (::bind(listenSocket, &acceptedSockAddr, sizeof(acceptedSockAddr)) == SOCKET_ERROR)
    {
        Logging::Error("[TcpListener] Bind adderss to the listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Get the socket file descriptor flags.
    int opts = ::fcntl(listenSocket, F_GETFL);
    if (opts == SOCKET_ERROR)
    {
        Logging::Error("[TcpListener] Get flags from the listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Set the non-blocking flag to the socket file descriptor.
    if (::fcntl(listenSocket, F_SETFL, opts | O_NONBLOCK) == SOCKET_ERROR)
    {
        Logging::Error("[TcpListener] Set the non-blocking flag to the listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Create epoll.
    epfd = ::epoll_create(maxConnections);
    if (epfd == INVALID_FD)
    {
        Logging::Error("[TcpListener] Create epoll from the listen socket failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }

    // Register epoll event.
    epEvent.events = EPOLLIN | EPOLLET;
    epEvent.data.fd = listenSocket;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, listenSocket, &epEvent))
    {
        Logging::Error("[TcpListener] Register the listen socket epoll event failed. ErrorCode: %d", errno);
        ::exit(EXIT_FAILURE);
    }
    epEventBuf = new epoll_event[maxConnections];

    char ip[INET6_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &((sockaddr_in*)&acceptedSockAddr)->sin_addr, ip, INET_ADDRSTRLEN);
    Logging::Info("[TcpListener] Create the listen socket success. Bind address: %s", ip);
}

TcpListener::~TcpListener()
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

    if (requestProducer != nullptr)
    {
        delete requestProducer;
    }

    for (auto it = connectionMap.begin(); it != connectionMap.end(); ++it)
    {
        delete it->second;
    }
}

inline void TcpListener::Listen()
{
    if (::listen(listenSocket, 128) == SOCKET_ERROR) // NOTE: backlog參數會與/proc/sys/net/core/somaxconn的值取最小值
    {
        Logging::Info("[TcpListener] The serve socket listen failed. ErrorCode: %d", errno);
        return;
    }

    receiveThread = new Thread(&TcpListener::ProcessEvents, this);
    sendThread = new Thread(&TcpListener::SendResponses, this);
    Logging::Info("[TcpListener] The server socket is listening.");
}

inline void TcpListener::Dispatch()
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
            delete *it;
        }
    }
    consumerRequests->clear();
}

inline void TcpListener::Send(ResponseBase* response)
{
    responseLocker.lock();
    producerResponses->push_back(response);
    hasNewResponse = true;
    responseCv.notify_one();
    responseLocker.unlock();
}

void TcpListener::ProcessEvents()
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

            Logging::Info("[TcpListener] Wait for epoll events failed. ErrorCode: %d", errorCode);
            // TODO: Find an appropriate way to handle errors.
            continue;
        }

        while (evtNum-- > 0)
        {
            sock = epEventBuf[evtNum].data.fd;
            if (sock == listenSocket)
            {
                Accept();
                continue;
            }

            if (epEventBuf[evtNum].events & EPOLLIN)
            {
                Receive(sock);
            }
        }
    }
}

inline void TcpListener::Accept()
{
    while (true)
    {
        SOCKET sock = ::accept(listenSocket, &acceptedSockAddr, &acceptedSockAddrLen);
        if (sock == SOCKET_ERROR)
        {
            int errorCode = errno;
            if (errorCode == EAGAIN || errorCode == EWOULDBLOCK)
            {
                break;
            }

            Logging::Error("[TcpListener] Accept socket failed. ErrorCode: %d", errno);
            continue;
        }

        int opts = ::fcntl(listenSocket, F_GETFL);
        if (opts == SOCKET_ERROR)
        {
            Logging::Error("[TcpListener] Get the socket flags when accept failed. ErrorCode: %d", errno);
            continue;
        }

        if (::fcntl(sock, F_SETFL, opts | O_NONBLOCK) == SOCKET_ERROR)
        {
            Logging::Error("[TcpListener] Set the socket flags when accept failed. ErrorCode: %d", errno);
            continue;
        }

        epEvent.events = EPOLLIN | EPOLLET;
        epEvent.data.fd = sock;
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &epEvent))
        {
            Logging::Error("[TcpListener] Register the accepted socket epoll event failed. ErrorCode: %d", errno);
        }

        Connection* conn = connectionMap[sock];
        if (conn == nullptr)
        {
            conn = new Connection();
            conn->readState.packetBuf.final = new char[maxPacketBytes * 4];
            conn->readState.packetBuf.compress = new char[maxPacketBytes];
            conn->readState.packetBuf.encrypt = new char[maxPacketBytes];
            connectionMap[sock] = conn;
        }
        conn->connId.sock = sock;
        conn->connId.addr = acceptedSockAddr;
        conn->readState.isWaitingPacketSize = true;
        conn->readState.waitingBytes = sizeof(PacketLengthSize);
        conn->readState.pendingBytes = 0;
        conn->readState.processedBytes = 0;
    }
}

inline void TcpListener::Receive(SOCKET sock)
{
    Connection* conn = connectionMap[sock];
    PacketReadState* readState = &conn->readState;
    while (true)
    {
        ssize_t transferredBytes = ::recv(sock, readState->packetBuf.final + readState->pendingBytes, maxPacketBytes - readState->pendingBytes, 0);
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

            ::close(sock);
            break;
        }

        if (transferredBytes == 0)
        {
            ::close(sock);
            break;
        }

        readState->pendingBytes += transferredBytes;
        while (readState->pendingBytes >= readState->waitingBytes)
        {
            if (readState->isWaitingPacketSize)
            {
                readState->isWaitingPacketSize = false;
                readState->waitingBytes = *reinterpret_cast<PacketLengthSize*>(readState->packetBuf.final + readState->processedBytes);
                readState->pendingBytes -= sizeof(PacketLengthSize);
                readState->processedBytes += sizeof(PacketLengthSize);
                continue;
            }

            readState->packetBuf.offset = readState->processedBytes;
            RequestBase* req = requestProducer->Produce(readState->packetBuf, &(conn->connId));
            if (req == nullptr)
            {
                Logging::Error("[TcpListener] Create resquest failed. socket: %p.");
                ::close(sock);
                break;
            }
            requestLocker.lock();
            producerRequests->push_back(req);
            requestLocker.unlock();

            readState->pendingBytes -= readState->waitingBytes;
            readState->processedBytes += readState->waitingBytes;
            readState->isWaitingPacketSize = true;
            readState->waitingBytes = sizeof(PacketLengthSize);
        }

        if (readState->pendingBytes > 0)
        {
            // TODO: Make sure an overlapping array don't cause problems.
            ::memcpy(readState->packetBuf.final, readState->packetBuf.final + readState->processedBytes, readState->pendingBytes);
        }
        readState->processedBytes = 0;
    }
}

void TcpListener::SendResponses()
{
    char sendBuf[maxPacketBytes * 16];
    char compressBuf[maxPacketBytes];
    char encryptBuf[maxPacketBytes];
    PacketBuffer packetBuf{ 0, sendBuf, compressBuf, encryptBuf };
    UniqueLock<Mutex> uniqueLock(responseLocker, std::defer_lock);

    while (true)
    {
        Container::Vector<ResponseBase*>* temp = consumerResponses;
        consumerResponses = producerResponses;
        uniqueLock.lock();
        while (!hasNewResponse)
        {
            responseCv.wait(uniqueLock, [this] { return hasNewResponse; });
        }

        producerResponses = temp;
        hasNewResponse = false;
        uniqueLock.unlock();

        for (auto respIt = consumerResponses->begin(); respIt != consumerResponses->end(); ++respIt)
        {
            packetBuf.offset = 0;
            ResponseBase& resp = **respIt;
            int32 pendingBytes = resp.Pack(packetBuf);
            for (auto connIt = resp.connIds.begin(); connIt != resp.connIds.end(); ++connIt)
            {
                int32 sentBytes = 0;
                while (sentBytes < pendingBytes)
                {
                    // TODO: Use async to send
                    ssize_t result = ::send((*connIt)->sock, sendBuf + sentBytes, pendingBytes - sentBytes, 0);
                    if (result == SOCKET_ERROR)
                    {
                        // TODO: Process error conditions.
                        Logging::Error("[TcpListener] Send failed. ErrorCode: %d", errno);
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