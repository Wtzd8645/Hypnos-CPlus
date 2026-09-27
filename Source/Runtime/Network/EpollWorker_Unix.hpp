#pragma once

#include "DeliveryEvent.hpp"
#include "WorkerCommand.hpp"

#include <Hypnos-Core/Container/SpscRingBuffer.hpp>

#include <memory>

#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <unordered_map>
#endif

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct ConnectionSlot;
struct Endpoint;
struct NetworkCore;

#if defined(__linux__)

enum class FdKind : uint8
{
    Listen,
    Connection
};

struct FdBinding
{
    FdKind kind = FdKind::Listen;
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    uint32 slot = INVALID_SLOT;
    uint32 generation = 0;
};

class EpollWorker
{
public:
    uint16 id = 0;
    Atomic<bool> is_running { false };
    int32 epoll_fd = INVALID_FD;
    int32 event_fd = INVALID_FD;
    Thread thread;
    UniquePtr<SpscRingBuffer<WorkerCommand>> command_queue;
    UniquePtr<SpscRingBuffer<DeliveryEvent>> delivery_queue;
    std::unordered_map<int32, FdBinding> fd_bindings;

    Status<void> Initialize(uint16 worker_id, size_t command_capacity, size_t delivery_capacity)
    {
        id = worker_id;
        command_queue = std::make_unique<SpscRingBuffer<WorkerCommand>>(command_capacity);
        delivery_queue = std::make_unique<SpscRingBuffer<DeliveryEvent>>(delivery_capacity);

        epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd == INVALID_FD)
        {
            return NetworkError(NetworkStatus::TransportError, "[EpollBackend] Failed to create epoll fd.");
        }

        event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (event_fd == INVALID_FD)
        {
            return NetworkError(NetworkStatus::TransportError, "[EpollBackend] Failed to create eventfd.");
        }

        epoll_event event { };
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = event_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &event) != 0)
        {
            return NetworkError(NetworkStatus::TransportError, "[EpollBackend] Failed to register eventfd.");
        }

        return Status<void>::Success();
    }

    Status<void> AddListenSocket(EndpointId endpoint_id, int32 listen_fd)
    {
        epoll_event event { };
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = listen_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) != 0)
        {
            return NetworkError(NetworkStatus::TransportError, "[EpollBackend] Failed to register listen socket.");
        }

        fd_bindings[listen_fd] = FdBinding { FdKind::Listen, endpoint_id, INVALID_SLOT, 0 };
        return Status<void>::Success();
    }

    bool EnqueueCommand(const WorkerCommand& command)
    {
        if (command_queue == nullptr || !command_queue->Enqueue(command))
        {
            return false;
        }

        uint64 value = 1;
        (void)write(event_fd, &value, sizeof(value));
        return true;
    }

    void Start(NetworkCore& network_core);
    void Stop();
    void Run(NetworkCore& network_core);
    void DrainWake();
    void ProcessCommands(NetworkCore& network_core);
    void HandleCommand(NetworkCore& network_core, const WorkerCommand& command);
    void HandleListen(NetworkCore& network_core, EndpointId endpoint_id, int32 fd);
    void HandleConnection(NetworkCore& network_core, int32 fd, uint32 events);
    void HandleReadable(NetworkCore& network_core, Endpoint& endpoint, ConnectionSlot& connection);
    void HandleWritable(NetworkCore& network_core, ConnectionSlot& connection);
    void FailConnection(NetworkCore& network_core, Endpoint& endpoint, ConnectionSlot& connection, NetworkStatus status, const char* detail);
    void CloseConnection(NetworkCore& network_core, Endpoint& endpoint, ConnectionSlot& connection, ConnectionEventType event_type, NetworkStatus status, const char* detail);
    Status<void> RegisterConnectionSocket(Endpoint& endpoint, ConnectionSlot& connection);
    void StartClientConnect(NetworkCore& network_core, Endpoint& endpoint);
};

#else

class EpollWorker
{
};

#endif

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
