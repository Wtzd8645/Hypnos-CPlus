#include <Foundation/Logging.hpp>
#include "TcpListener.hpp"

namespace Blanketmen {
namespace Hypnos {

TcpListener::TcpListener(int32 maxConns, RequestHandlerBase* requestFcty, Dictionary<uint16, Delegate<RequestBase*>*> reqHandlerDict) :
    iocpThreads(8),
    maxConnections(maxConns),
    requestFactory(requestFcty),
    requestHandlerDict(reqHandlerDict)
{
    int32 messagePoolSize = maxConns * 4;
    producerRequests = new SLinkedList<RequestBase*>(messagePoolSize);
    consumerRequests = new SLinkedList<RequestBase*>(messagePoolSize);
    producerResponses = new SLinkedList<ResponseBase*>(messagePoolSize);
    consumerResponses = new SLinkedList<ResponseBase*>(messagePoolSize);

    ::InitializeCriticalSection(&clientLocker);
    ::InitializeCriticalSection(&requestLocker);
    WSADATA wsaData;
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        Logging::Error("[NetworkManager] WSAStartup failed. ErrorCode: %d", ::WSAGetLastError());
    }
    else
    {
        Logging::Info("[NetworkManager] WSAStartup success.");
    }
    addrinfo hints;
    ::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* addrInfo = nullptr;
    if (::getaddrinfo(NULL, DEFAULT_PORT, &hints, &addrInfo) != 0)
    {
        Logging::Error("[NetworkManager] getaddrinfo failed. ErrorCode: %d", ::WSAGetLastError());
        return;
    }

    // Create listen socket
    listenSocket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED); // 不是非阻塞套接字，但是重叠I/O套接字。
    if (listenSocket == INVALID_SOCKET)
    {
        Logging::Error("[NetworkManager] WSASocket failed. ErrorCode: %d", ::WSAGetLastError());
        return;
    }

    // Set NoDelay
    int yes = 1;
    ::setsockopt(listenSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(int));

    // Show listen IP
    char ipAddr[INET_ADDRSTRLEN];
    ::inet_ntop(addrInfo->ai_family, addrInfo->ai_addr, ipAddr, sizeof(ipAddr));
    Logging::Info("[NetworkManager] Bind IP: %s", ipAddr);

    // Bind
    if (::bind(listenSocket, addrInfo->ai_addr, (int)addrInfo->ai_addrlen) == SOCKET_ERROR)
    {
        Logging::Error("[NetworkManager] Bind failed. ErrorCode: %ld", ::WSAGetLastError());
        ::closesocket(listenSocket);
    }
    ::freeaddrinfo(addrInfo);
}

TcpListener::~TcpListener()
{
    ::DeleteCriticalSection(&clientLocker);
    ::DeleteCriticalSection(&requestLocker);
    for (auto it = iocpThreads.begin(); it != iocpThreads.end(); ++it)
    {
        (*it)->detach();
        delete* it;
    }
    if (receiveThread != nullptr)
    {
        receiveThread->detach();
        delete receiveThread;
    }
    ::closesocket(listenSocket);
    ::WSACleanup();

    if (requestFactory != nullptr)
    {
        delete requestFactory;
    }
    delete producerRequests;
    delete consumerRequests;
    delete producerResponses;
    delete consumerResponses;
}

inline void TcpListener::Listen()
{
    if (::listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        Logging::Error("[NetworkManager] Listen failed. ErrorCode: %d", ::WSAGetLastError());
        ::closesocket(listenSocket);
        return;
    }

    iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

    // 建立執行緒處理socket接收
    SYSTEM_INFO sysInfo;
    ::GetSystemInfo(&sysInfo);
    int32 useThreadCount = sysInfo.dwNumberOfProcessors - 2;
    if (useThreadCount < 1)
    {
        useThreadCount = 1;
    }

    Logging::Info("[NetworkManager] Start listen. UseThreadCount: %d", useThreadCount);
    for (int i = 0; i < useThreadCount; ++i)
    {
        iocpThreads.AddLast(new std::thread(&NetworkManager::ProcessEvents, this)); //_beginthreadex(NULL, 0, &NetworkManager::Process, (LPVOID)iocpHandle, 0, NULL);
    }
    receiveThread = new std::thread(&NetworkManager::Accept, this);
}

inline void TcpListener::Dispatch()
{
    ::EnterCriticalSection(&requestLocker);
    for (auto it = consumerRequests.begin(); it != consumerRequests.end(); ++it)
    {
        Delegate<RequestBase*>* handler = requestHandlerDict[(*it)->header.msgId];
        if (handler == nullptr)
        {
            continue;
        }
        (*handler)(*it);
    }
    consumerRequests.Clear();
    ::LeaveCriticalSection(&requestLocker);
}

void TcpListener::ProcessEvents()
{
    DWORD bytesTransferred;
    Connection* client = nullptr;
    IocpEventArgs* iocpArgs = nullptr;
    while (true)
    {
        ::GetQueuedCompletionStatus(iocpHandle, &bytesTransferred, (PULONG_PTR)&client, (LPOVERLAPPED*)&iocpArgs, INFINITE);
        if (bytesTransferred == 0) // 連接結束
        {
            Logging::Info("[NetworkManager] Socket closed: %p", client);
            ::closesocket(client->socket);
            delete iocpArgs;
            continue;
        }

        if (iocpArgs->operation != IocpOperation::IOCP_RECEIVE)
        {
            delete iocpArgs;
            continue;
        }

        Logging::Info("[NetworkManager] Receive: %d bytes", bytesTransferred);
        uint32 bytesProcessed = 0;
        while (bytesTransferred > 0)
        {
            uint32 bytesUnreceived = client->waitingBytes - client->pendingBytes;
            if (bytesTransferred < bytesUnreceived)
            {
                ::memcpy(&client->receiveBuffer + client->pendingBytes, iocpArgs->wsaBuffer.buf + bytesProcessed, bytesTransferred);
                client->pendingBytes += bytesTransferred;
                break;
            }

            char_ptr dataBuf;
            if (client->pendingBytes != 0u)
            {
                ::memcpy(&client->receiveBuffer + client->pendingBytes, iocpArgs->wsaBuffer.buf + bytesProcessed, bytesUnreceived);
                dataBuf = client->receiveBuffer;
            }
            else
            {
                dataBuf = iocpArgs->wsaBuffer.buf + bytesProcessed;
            }
            bytesTransferred -= bytesUnreceived;
            bytesProcessed += bytesUnreceived;

            if (client->isWaitingPacketSize)
            {
                client->isWaitingPacketSize = false;
                client->waitingBytes = *reinterpret_cast<packet_size*>(dataBuf);
                client->pendingBytes = 0u;
                continue;
            }
            client->isWaitingPacketSize = true;
            client->waitingBytes = PACKET_LEN_SIZE;
            client->pendingBytes = 0u;

            uint8 seq = *reinterpret_cast<uint8_ptr>(dataBuf);
            if (seq != client->serialNumber)
            {
                Logging::Error("[NetworkManager] SerialNumber is not equal. Client: %p", client);
                ::closesocket(client->socket);
                delete iocpArgs;
                continue;
            }
            ++client->serialNumber;

            RequestBase* request = requestFactory->Create(dataBuf + sizeof(seq));
            if (request != nullptr)
            {
                request->client = client;
                ::EnterCriticalSection(&requestLocker);
                consumerRequests.AddLast(request);
                ::LeaveCriticalSection(&requestLocker);
                Logging::Info("[NetworkManager] Unpack message successfully. msgId: %d", request->header.msgId);
            }
        }

        // Continue receive
        Receive(client, iocpArgs);
    }
}

void TcpListener::Accept()
{
    Logging::Info("[NetworkManager] Accept socket.");
    sockaddr addr;
    int addrSize = sizeof(addr);
    while (true)
    {
        SOCKET acceptedSocket = ::accept(listenSocket, &addr, &addrSize);
        if (acceptedSocket == INVALID_SOCKET)
        {
            if (::WSAGetLastError() == WSAEWOULDBLOCK)
            {
                continue;
            }

            Logging::Error("[NetworkManager] Accept failed. ErrorCode: %d", ::WSAGetLastError());
            continue;
        }

        Logging::Info("[NetworkManager] Accept socket successfully.");
        Connection* client = new Connection();
        client->socket = acceptedSocket;

        // Bind Handle and CompletionPort
        ::CreateIoCompletionPort((HANDLE)acceptedSocket, iocpHandle, (ULONG_PTR)client, 0); // TODO: To understanding NumberOfConcurrentThreads

        // Start recevie
        Receive(client, new IocpEventArgs());
    }
}

inline void TcpListener::Receive(Connection* client, IocpEventArgs* iocpArgs)
{
    DWORD bytesRecvd = 0;
    ::WSARecv(client->socket, &(iocpArgs->wsaBuffer), 1, &bytesRecvd, &clientRecvFlags, (LPWSAOVERLAPPED)iocpArgs, NULL);

    // Complete immediately
    if (bytesRecvd != 0) // TODO: 立即完成還是會發I/O消息，不知道是否需要處理這情況
    {
        Logging::InfoWarning("[NetworkManager] Receive %d bytes immediately.", bytesRecvd);
    }
}

void TcpListener::Send(ResponseBase* response)
{
    // Temp echo
    // IocpEventArgs* iocpArgs = new IocpEventArgs(client->receiveBuffer, client->totalPacketSize);
    // ::WSASend(client->socket, &(iocpArgs->wsaBuffer), 1, NULL, 0, (LPWSAOVERLAPPED)iocpArgs, NULL);
    IocpEventArgs* iocpArgs = new IocpEventArgs(response);
    ::WSASend(response->client->socket, &(iocpArgs->wsaBuffer), 1, NULL, 0, (LPWSAOVERLAPPED)iocpArgs, NULL);
}

} // namespace Hypnos
} // namespace Blanketmen