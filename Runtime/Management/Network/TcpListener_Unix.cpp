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
    if (listenSocket == INVALID_FD)
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

    if (listenSocket != INVALID_FD)
    {
        ::close(listenSocket);
    }

    if (request_factory != nullptr)
    {
        delete request_factory;
    }

    for (auto it = connection_map.begin(); it != connection_map.end(); ++it)
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
    Container::Vector<RequestBase*>* temp = consumer_requests;
    consumer_requests = producer_requests;
    request_mutex.lock();
    producer_requests = temp;
    request_mutex.unlock();

    for (auto it = consumer_requests->begin(); it != consumer_requests->end(); ++it)
    {
        Delegate<RequestBase&>* handler = requestHandlerMap[(*it)->header.msgId];
        if (handler != nullptr)
        {
            (*handler)(**it);
            delete *it;
        }
    }
    consumer_requests->clear();
}

inline void TcpListener::Send(ResponseBase* response)
{
    responseLocker.lock();
    producer_responses->push_back(response);
    hasNewResponse = true;
    responseCv.notify_one();
    responseLocker.unlock();
}

void TcpListener::ProcessEvents()
{
    int evtNum = 0;
    Socket sock = 0;
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
        Socket sock = ::accept(listenSocket, &acceptedSockAddr, &acceptedSockAddrLen);
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

        Connection* conn = connection_map[sock];
        if (conn == nullptr)
        {
            conn = new Connection();
            conn->recv_ctx.buffer = new char[maxPacketBytes * 4];
            connection_map[sock] = conn;
        }
        conn->sock = sock;
        ::memcpy(&conn->addr, &acceptedSockAddr, sizeof(sockaddr));
        conn->recv_ctx.packet_bytes = 0;
        conn->recv_ctx.pending_bytes = sizeof(PacketLengthSize);
        conn->recv_ctx.buffer_bytes = 0;
        conn->recv_ctx.processed_bytes = 0;
    }
}

inline void TcpListener::Receive(Socket sock)
{
    Connection* conn = connection_map[sock];
    PacketContext* ctx = &conn->recv_ctx;
    while (true)
    {
        ssize_t transferredBytes = ::recv(sock, ctx->buffer + ctx->buffer_bytes, maxPacketBytes - ctx->buffer_bytes, 0);
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

        ctx->buffer_bytes += transferredBytes;
        while (ctx->buffer_bytes >= ctx->pending_bytes)
        {
            if (ctx->packet_bytes == 0)
            {
                ctx->packet_bytes = *reinterpret_cast<PacketLengthSize*>(ctx->buffer + ctx->processed_bytes);
                ctx->pending_bytes = ctx->packet_bytes;
                ctx->buffer_bytes -= sizeof(PacketLengthSize);
                ctx->processed_bytes += sizeof(PacketLengthSize);
                continue;
            }

            RequestBase* req = request_factory->Create(ctx->buffer + ctx->processed_bytes, conn);
            if (req == nullptr)
            {
                Logging::Error("[TcpListener] Create resquest failed. socket: %p.");
                ::close(sock);
                break;
            }
            request_mutex.lock();
            producer_requests->push_back(req);
            request_mutex.unlock();

            ctx->buffer_bytes -= ctx->pending_bytes;
            ctx->processed_bytes += ctx->pending_bytes;
            ctx->packet_bytes = 0;
            ctx->pending_bytes = sizeof(PacketLengthSize);
        }

        if (ctx->buffer_bytes > 0)
        {
            // TODO: Make sure an overlapping array don't cause problems.
            ::memcpy(ctx->buffer, ctx->buffer + ctx->processed_bytes, ctx->buffer_bytes);
        }
        ctx->processed_bytes = 0;
    }
}

void TcpListener::SendResponses()
{
    char sendBuf[maxPacketBytes * 16];
    char compressBuf[maxPacketBytes];
    char encryptBuf[maxPacketBytes];
    PacketBuffer buffer{ sendBuf, 0 };
    UniqueLock<Mutex> uniqueLock(responseLocker, std::defer_lock);

    while (true)
    {
        Container::Vector<ResponseBase*>* temp = consumer_responses;
        consumer_responses = producer_responses;
        uniqueLock.lock();
        while (!hasNewResponse)
        {
            responseCv.wait(uniqueLock, [this] { return hasNewResponse; });
        }

        producer_responses = temp;
        hasNewResponse = false;
        uniqueLock.unlock();

        for (auto respIt = consumer_responses->begin(); respIt != consumer_responses->end(); ++respIt)
        {
            buffer.offset = 0;
            ResponseBase& resp = **respIt;
            int32 buffer_bytes = resp.Pack(buffer);
            for (auto connIt = resp.conns.begin(); connIt != resp.conns.end(); ++connIt)
            {
                int32 sentBytes = 0;
                while (sentBytes < buffer_bytes)
                {
                    // TODO: Use async to send
                    ssize_t result = ::send((*connIt)->sock, sendBuf + sentBytes, buffer_bytes - sentBytes, 0);
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
        consumer_responses->clear();
    }
}

} // namespace Hypnos
} // namespace Blanketmen