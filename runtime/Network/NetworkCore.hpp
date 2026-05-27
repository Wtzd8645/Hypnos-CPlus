#pragma once

#include <Hypnos/Network/NetworkManager.hpp>
#include <Hypnos-Core/Container/SpscRingBuffer.hpp>

#include "PacketFraming.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#endif

namespace Blanketmen {
namespace Hypnos {
namespace Network {

constexpr int32 INVALID_FD = -1;
constexpr uint32 INVALID_SLOT = static_cast<uint32>(~0u);

inline Status<void> NetworkError(NetworkStatus status, const char* message)
{
    return Status<void>::Error(ToErrorCode(status), message);
}

template<typename T>
class BoundedQueue
{
public:
    Status<void> Initialize(size_t queue_capacity)
    {
        if (queue_capacity == 0)
        {
            return NetworkError(NetworkStatus::InvalidConfig, "[Network] Queue capacity must be greater than zero.");
        }

        items.clear();
        items.resize(queue_capacity);
        head = 0;
        tail = 0;
        count = 0;
        return Status<void>::Success();
    }

    bool Enqueue(const T& item)
    {
        if (IsFull())
        {
            return false;
        }

        items[tail] = item;
        tail = (tail + 1) % items.size();
        ++count;
        return true;
    }

    bool Dequeue(T& item)
    {
        if (IsEmpty())
        {
            return false;
        }

        item = items[head];
        head = (head + 1) % items.size();
        --count;
        return true;
    }

    void Clear() noexcept
    {
        head = 0;
        tail = 0;
        count = 0;
    }

    bool IsEmpty() const noexcept { return count == 0; }
    bool IsFull() const noexcept { return !items.empty() && count == items.size(); }
    size_t Capacity() const noexcept { return items.size(); }

private:
    List<T> items;
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;
};

enum class EndpointKind : uint8
{
    Server,
    Client
};

enum class DeliveryType : uint8
{
    Message,
    Connection,
    Error
};

enum class WorkerCommandType : uint8
{
    ConnectClient,
    DisconnectClient,
    Send,
    Close,
    Shutdown
};

struct WorkerCommand
{
    WorkerCommandType type = WorkerCommandType::Shutdown;
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    ConnectionHandle connection;
    uint32 packet_slot = INVALID_SLOT;
};

struct DeliveryEvent
{
    DeliveryType type = DeliveryType::Error;
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    ConnectionHandle connection;
    ConnectionEventType connection_event_type = ConnectionEventType::Failed;
    ConnectionState connection_state = ConnectionState::Failed;
    NetworkStatus status = NetworkStatus::TransportError;
    const char* detail = nullptr;
    uint32 packet_slot = INVALID_SLOT;
};

struct PacketStorage
{
    List<byte> bytes;
    uint32 size = 0;
    uint8 codec_id = 0;
    bool in_use = false;

    Status<void> Initialize(uint32 payload_capacity)
    {
        bytes.clear();
        bytes.resize(static_cast<size_t>(PACKET_HEADER_SIZE) + payload_capacity);
        size = 0;
        codec_id = 0;
        in_use = false;
        return Status<void>::Success();
    }
};

struct NetworkCore;
struct Endpoint;

struct ConnectionSlot
{
    Endpoint* endpoint = nullptr;
    uint32 slot = 0;
    uint32 generation = 1;
    int32 fd = INVALID_FD;
    ConnectionState state = ConnectionState::Closed;
    List<PacketStorage> send_slots;
    List<PacketStorage> receive_slots;
    BoundedQueue<uint32> pending_send_slots;
    List<byte> stream_buffer;
    size_t stream_size = 0;
    uint32 active_send_slot = INVALID_SLOT;
    size_t active_send_offset = 0;

    Status<void> Initialize(
        Endpoint* owner,
        uint32 slot_index,
        uint32 send_capacity,
        uint32 receive_capacity,
        uint32 codec_buffer_capacity)
    {
        endpoint = owner;
        slot = slot_index;
        generation = 1;
        fd = INVALID_FD;
        state = ConnectionState::Closed;
        stream_size = 0;
        active_send_slot = INVALID_SLOT;
        active_send_offset = 0;

        send_slots.clear();
        send_slots.resize(send_capacity);
        Status<void> send_queue_status = pending_send_slots.Initialize(send_capacity);
        if (send_queue_status.IsFailed())
        {
            return send_queue_status;
        }

        for (PacketStorage& packet : send_slots)
        {
            Status<void> status = packet.Initialize(static_cast<PacketSize>(codec_buffer_capacity));
            if (status.IsFailed())
            {
                return status;
            }
        }

        receive_slots.clear();
        receive_slots.resize(receive_capacity);
        for (PacketStorage& packet : receive_slots)
        {
            Status<void> status = packet.Initialize(static_cast<PacketSize>(codec_buffer_capacity));
            if (status.IsFailed())
            {
                return status;
            }
        }

        stream_buffer.clear();
        stream_buffer.resize(static_cast<size_t>(PACKET_HEADER_SIZE) + codec_buffer_capacity);
        return Status<void>::Success();
    }

    ConnectionHandle Handle() const noexcept
    {
        return ConnectionHandle(EndpointIdValue(), slot, generation);
    }

    EndpointId EndpointIdValue() const noexcept;

    uint32 AcquireSendSlot() noexcept
    {
        for (uint32 i = 0; i < send_slots.size(); ++i)
        {
            if (!send_slots[i].in_use)
            {
                send_slots[i].in_use = true;
                send_slots[i].size = 0;
                return i;
            }
        }
        return INVALID_SLOT;
    }

    uint32 AcquireReceiveSlot() noexcept
    {
        for (uint32 i = 0; i < receive_slots.size(); ++i)
        {
            if (!receive_slots[i].in_use)
            {
                receive_slots[i].in_use = true;
                receive_slots[i].size = 0;
                return i;
            }
        }
        return INVALID_SLOT;
    }

    void ReleaseSendSlot(uint32 packet_slot) noexcept
    {
        if (packet_slot < send_slots.size())
        {
            send_slots[packet_slot].in_use = false;
            send_slots[packet_slot].size = 0;
        }
    }

    void ReleaseReceiveSlot(uint32 packet_slot) noexcept
    {
        if (packet_slot < receive_slots.size())
        {
            receive_slots[packet_slot].in_use = false;
            receive_slots[packet_slot].size = 0;
        }
    }

    void ResetTransport() noexcept
    {
#if defined(__linux__)
        if (fd != INVALID_FD)
        {
            close(fd);
        }
#endif
        fd = INVALID_FD;
        state = ConnectionState::Closed;
        stream_size = 0;
        active_send_slot = INVALID_SLOT;
        active_send_offset = 0;
        pending_send_slots.Clear();

        for (PacketStorage& packet : send_slots)
        {
            packet.in_use = false;
            packet.size = 0;
        }

        for (PacketStorage& packet : receive_slots)
        {
            packet.in_use = false;
            packet.size = 0;
        }
    }
};

struct Endpoint
{
    NetworkCore* owner = nullptr;
    EndpointKind kind = EndpointKind::Server;
    EndpointId id = INVALID_ENDPOINT_ID;
    uint16 worker_id = 0;
    uint32 codec_buffer_capacity = 0;
    int32 listen_fd = INVALID_FD;
    Server* server = nullptr;
    Client* client = nullptr;
    ServerState server_state = ServerState::Stopped;
    ClientState client_state = ClientState::Disconnected;
    MessageHandler message_handler;
    ConnectionEventHandler connection_event_handler;
    ErrorHandler error_handler;
    EndpointAddress address;
    List<ConnectionSlot> connections;
    bool has_pending_terminal_error = false;
    DeliveryEvent terminal_error;

    Status<void> Register(MessageHandler handler);
    Status<void> Register(ConnectionEventHandler handler);
    Status<void> Register(ErrorHandler handler);
    Status<void> Send(ConnectionHandle connection, IMessage& message);
    Status<void> Send(IMessage& message);
    Status<void> Close(ConnectionHandle connection);
    Status<void> Connect();
    Status<void> Disconnect();
    Status<void> CheckRegistrationAllowed() const;
    Status<void> QueueMessage(ConnectionHandle connection, IMessage& message);

    Status<void> InitializeServer(NetworkCore& network_core, Server& server_ref, const ServerConfig& config)
    {
        owner = &network_core;
        kind = EndpointKind::Server;
        id = config.id;
        worker_id = config.worker_id;
        codec_buffer_capacity = config.codec_buffer_capacity;
        server = &server_ref;
        client = nullptr;
        server_state = ServerState::Stopped;
        client_state = ClientState::Disconnected;
        address = config.bind;
        listen_fd = INVALID_FD;
        has_pending_terminal_error = false;

        connections.clear();
        connections.resize(config.max_connections);
        for (uint32 i = 0; i < connections.size(); ++i)
        {
            Status<void> status = connections[i].Initialize(
                this,
                i,
                config.send_queue_capacity_per_connection,
                config.receive_queue_capacity_per_connection,
                config.codec_buffer_capacity);
            if (status.IsFailed())
            {
                return status;
            }
        }

        return Status<void>::Success();
    }

    Status<void> InitializeClient(NetworkCore& network_core, Client& client_ref, const ClientConfig& config)
    {
        owner = &network_core;
        kind = EndpointKind::Client;
        id = config.id;
        worker_id = config.worker_id;
        codec_buffer_capacity = config.codec_buffer_capacity;
        server = nullptr;
        client = &client_ref;
        server_state = ServerState::Stopped;
        client_state = ClientState::Disconnected;
        address = config.remote;
        listen_fd = INVALID_FD;
        has_pending_terminal_error = false;

        connections.clear();
        connections.resize(1);
        Status<void> status = connections[0].Initialize(
            this,
            0,
            config.send_queue_capacity,
            config.receive_queue_capacity,
            config.codec_buffer_capacity);
        if (status.IsFailed())
        {
            return status;
        }

        return Status<void>::Success();
    }

    ConnectionSlot* Resolve(ConnectionHandle handle) noexcept
    {
        if (!handle.IsValid() || handle.Endpoint() != id || handle.Slot() >= connections.size())
        {
            return nullptr;
        }

        ConnectionSlot& connection = connections[handle.Slot()];
        if (connection.generation != handle.Generation() || connection.state == ConnectionState::Closed)
        {
            return nullptr;
        }

        return &connection;
    }

    ConnectionSlot* AcquireConnection(int32 fd)
    {
        for (ConnectionSlot& connection : connections)
        {
            if (connection.state == ConnectionState::Closed)
            {
                ++connection.generation;
                if (connection.generation == 0)
                {
                    connection.generation = 1;
                }

                connection.fd = fd;
                connection.state = ConnectionState::Connected;
                connection.stream_size = 0;
                connection.active_send_slot = INVALID_SLOT;
                connection.active_send_offset = 0;
                return &connection;
            }
        }
        return nullptr;
    }

    void ClearDelivery() noexcept
    {
        has_pending_terminal_error = false;
    }

    void ResetTransport() noexcept
    {
#if defined(__linux__)
        if (listen_fd != INVALID_FD)
        {
            close(listen_fd);
        }
#endif
        listen_fd = INVALID_FD;
        ClearDelivery();

        for (ConnectionSlot& connection : connections)
        {
            connection.ResetTransport();
        }

        server_state = ServerState::Stopped;
        client_state = ClientState::Disconnected;
    }

    void QueueTerminalError(ConnectionHandle connection, NetworkStatus status, const char* detail) noexcept
    {
        if (!has_pending_terminal_error)
        {
            terminal_error = DeliveryEvent { };
            terminal_error.type = DeliveryType::Error;
            terminal_error.endpoint_id = id;
            terminal_error.connection = connection;
            terminal_error.status = status;
            terminal_error.detail = detail;
            has_pending_terminal_error = true;
        }
    }

    void EnqueueDeliveryOverflow(const DeliveryEvent& event) noexcept
    {
        QueueTerminalError(event.connection, NetworkStatus::ResourceExhausted, "[Network] Delivery queue is full.");
    }
};

inline EndpointId ConnectionSlot::EndpointIdValue() const noexcept
{
    return endpoint != nullptr ? endpoint->id : INVALID_ENDPOINT_ID;
}

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
    void HandleListen(NetworkCore& network_core, int32 fd);
    void HandleConnection(NetworkCore& network_core, int32 fd, uint32 events);
    void HandleReadable(NetworkCore& network_core, Endpoint& endpoint, ConnectionSlot& connection);
    void HandleWritable(NetworkCore& network_core, ConnectionSlot& connection);
    void FailConnection(NetworkCore& network_core, Endpoint& endpoint, ConnectionSlot& connection, NetworkStatus status, const char* detail);
    void CloseConnection(NetworkCore& network_core, Endpoint& endpoint, ConnectionSlot& connection, ConnectionEventType event_type, NetworkStatus status, const char* detail);
    Status<void> RegisterConnectionSocket(Endpoint& endpoint, ConnectionSlot& connection);
    Status<void> StartClientConnect(NetworkCore& network_core, Endpoint& endpoint);
};

#else

class EpollWorker
{
};

#endif

struct NetworkCore
{
    NetworkConfig config;
    List<Server> servers;
    List<Client> clients;
    List<UniquePtr<Endpoint>> endpoints;
    List<UniquePtr<EpollWorker>> workers;
    List<IMessage*> delivered_messages;
    Mutex mutex;
    ManagerState manager_state = ManagerState::Unconfigured;
    std::thread::id owner_thread_id { };
    bool is_dispatching_callbacks = false;
    bool is_dispatching_message_callback = false;

    Endpoint* FindEndpoint(EndpointId id) const noexcept
    {
        for (const UniquePtr<Endpoint>& endpoint : endpoints)
        {
            if (endpoint->id == id)
            {
                return endpoint.get();
            }
        }
        return nullptr;
    }

    Endpoint* FindEndpointForFd(int32 fd) const noexcept
    {
        for (const UniquePtr<Endpoint>& endpoint : endpoints)
        {
            if (endpoint->listen_fd == fd)
            {
                return endpoint.get();
            }

            for (const ConnectionSlot& connection : endpoint->connections)
            {
                if (connection.fd == fd)
                {
                    return endpoint.get();
                }
            }
        }
        return nullptr;
    }

    Server* FindServer(EndpointId id) noexcept
    {
        Endpoint* endpoint = FindEndpoint(id);
        return endpoint != nullptr ? endpoint->server : nullptr;
    }

    Client* FindClient(EndpointId id) noexcept
    {
        Endpoint* endpoint = FindEndpoint(id);
        return endpoint != nullptr ? endpoint->client : nullptr;
    }

    void ReleaseDeliveredMessages()
    {
        if (config.owned_objects.message_allocator != nullptr)
        {
            for (IMessage* message : delivered_messages)
            {
                if (message != nullptr)
                {
                    config.owned_objects.message_allocator->Release(*message);
                }
            }
        }

        delivered_messages.clear();
    }

    size_t WorkerCommandCapacity(uint16 worker_id) const noexcept
    {
        size_t capacity = 1;
        for (const ServerConfig& server_config : config.servers)
        {
            if (server_config.worker_id == worker_id)
            {
                capacity += static_cast<size_t>(server_config.max_connections)
                    * (static_cast<size_t>(server_config.send_queue_capacity_per_connection) + 2);
            }
        }

        for (const ClientConfig& client_config : config.clients)
        {
            if (client_config.worker_id == worker_id)
            {
                capacity += static_cast<size_t>(client_config.send_queue_capacity) + 2;
            }
        }

        return capacity;
    }

    size_t WorkerDeliveryCapacity(uint16 worker_id) const noexcept
    {
        size_t capacity = 1;
        for (const ServerConfig& server_config : config.servers)
        {
            if (server_config.worker_id == worker_id)
            {
                capacity += server_config.delivery_queue_capacity;
            }
        }

        for (const ClientConfig& client_config : config.clients)
        {
            if (client_config.worker_id == worker_id)
            {
                capacity += client_config.delivery_queue_capacity;
            }
        }

        return capacity;
    }

    bool QueueDelivery(Endpoint& endpoint, const DeliveryEvent& event)
    {
#if defined(__linux__)
        EpollWorker* worker = workers[endpoint.worker_id].get();
        if (worker != nullptr && worker->delivery_queue != nullptr && worker->delivery_queue->Enqueue(event))
        {
            return true;
        }
#else
        (void)event;
#endif

        endpoint.EnqueueDeliveryOverflow(event);
        if (event.connection.IsValid())
        {
            ConnectionSlot* connection = endpoint.Resolve(event.connection);
            if (connection != nullptr)
            {
                connection->state = ConnectionState::Failed;
            }
        }
        else if (endpoint.kind == EndpointKind::Server)
        {
            endpoint.server_state = ServerState::Failed;
        }
        else if (endpoint.kind == EndpointKind::Client)
        {
            endpoint.client_state = ClientState::Failed;
        }

        return false;
    }

    Status<void> Start();
    Status<void> Stop();
    Status<void> DispatchCallbacks(NetworkManager& manager);
    void CleanupTransport();
};

#if defined(__linux__)
int32 SetNonBlocking(int32 fd);
Status<void> FillAddress(const EndpointAddress& address, sockaddr_storage& storage, socklen_t& length);
Status<int32> OpenListenSocket(const EndpointAddress& address);
Status<int32> OpenClientSocket(const EndpointAddress& address, bool& is_pending);
#endif

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
