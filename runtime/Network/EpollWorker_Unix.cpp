#include "EpollWorker_Unix.hpp"

#include "NetworkCore.hpp"
#include "SocketUtils_Unix.hpp"
#include "TcpPacketAssembler.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

#if defined(__linux__)
void EpollWorker::Start(NetworkCore& network_core)
{
    is_running.store(true, std::memory_order_release);
    thread = Thread([this, &network_core]() { Run(network_core); });
}

void EpollWorker::Stop()
{
    if (is_running.load(std::memory_order_acquire))
    {
        is_running.store(false, std::memory_order_release);
        uint64 value = 1;
        (void)write(event_fd, &value, sizeof(value));

        if (thread.joinable())
        {
            thread.join();
        }
    }

    if (event_fd != INVALID_FD)
    {
        close(event_fd);
        event_fd = INVALID_FD;
    }

    if (epoll_fd != INVALID_FD)
    {
        close(epoll_fd);
        epoll_fd = INVALID_FD;
    }

    fd_bindings.clear();
    command_queue.reset();
    delivery_queue.reset();
}

void EpollWorker::Run(NetworkCore& network_core)
{
    constexpr int32 MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (is_running.load(std::memory_order_acquire))
    {
        int32 count = epoll_wait(epoll_fd, events, MAX_EVENTS, 25);
        if (count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        for (int32 i = 0; i < count; ++i)
        {
            int32 fd = events[i].data.fd;
            if (fd == event_fd)
            {
                DrainWake();
                ProcessCommands(network_core);
                continue;
            }

            auto binding_it = fd_bindings.find(fd);
            if (binding_it == fd_bindings.end())
            {
                continue;
            }

            if (binding_it->second.kind == FdKind::Listen)
            {
                HandleListen(network_core, binding_it->second.endpoint_id, fd);
            }
            else
            {
                HandleConnection(network_core, fd, events[i].events);
            }
        }

        ProcessCommands(network_core);
    }
}

void EpollWorker::DrainWake()
{
    uint64 value = 0;
    while (read(event_fd, &value, sizeof(value)) == sizeof(value))
    {
    }
}

void EpollWorker::ProcessCommands(NetworkCore& network_core)
{
    while (true)
    {
        WorkerCommand command { };
        if (command_queue == nullptr || !command_queue->Dequeue(command))
        {
            return;
        }

        HandleCommand(network_core, command);
        if (command.type == WorkerCommandType::Shutdown)
        {
            return;
        }
    }
}

void EpollWorker::HandleCommand(NetworkCore& network_core, const WorkerCommand& command)
{
    if (command.type == WorkerCommandType::Shutdown)
    {
        is_running.store(false, std::memory_order_release);
        return;
    }

    LockGuard<Mutex> lock(network_core.mutex);
    Endpoint* endpoint = network_core.FindEndpoint(command.endpoint_id);
    if (endpoint == nullptr)
    {
        return;
    }

    if (command.type == WorkerCommandType::ConnectClient)
    {
        StartClientConnect(network_core, *endpoint);
        return;
    }

    ConnectionSlot* connection = endpoint->Resolve(command.connection);
    if (connection == nullptr)
    {
        return;
    }

    if (command.type == WorkerCommandType::Send)
    {
        if (connection->state != ConnectionState::Connected)
        {
            connection->ReleaseSendSlot(command.packet_slot);
            return;
        }

        if (connection->active_send_slot == INVALID_SLOT)
        {
            connection->active_send_slot = command.packet_slot;
            connection->active_send_offset = 0;
            HandleWritable(network_core, *connection);
        }
        else
        {
            if (!connection->pending_send_slots.Enqueue(command.packet_slot))
            {
                connection->ReleaseSendSlot(command.packet_slot);
                FailConnection(network_core, *endpoint, *connection, NetworkStatus::ResourceExhausted, "[EpollBackend] Pending send queue overflow.");
            }
        }
        return;
    }

    if (command.type == WorkerCommandType::Close || command.type == WorkerCommandType::DisconnectClient)
    {
        CloseConnection(network_core, *endpoint, *connection, ConnectionEventType::Closed, NetworkStatus::TransportError, nullptr);
    }
}

void EpollWorker::HandleListen(NetworkCore& network_core, EndpointId endpoint_id, int32 fd)
{
    while (true)
    {
        sockaddr_storage storage { };
        socklen_t length = sizeof(storage);
        int32 accepted_fd = accept4(fd, reinterpret_cast<sockaddr*>(&storage), &length, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (accepted_fd == INVALID_FD)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }
            return;
        }

        LockGuard<Mutex> lock(network_core.mutex);
        Endpoint* endpoint = network_core.FindEndpoint(endpoint_id);
        if (endpoint == nullptr)
        {
            close(accepted_fd);
            return;
        }

        ConnectionSlot* connection = endpoint->AcquireConnection(accepted_fd);
        if (connection == nullptr)
        {
            close(accepted_fd);
            endpoint->QueueTerminalError(ConnectionHandle { }, NetworkStatus::ResourceExhausted, "[EpollBackend] Server connection pool is full.");
            return;
        }

        Status<void> register_status = RegisterConnectionSocket(*endpoint, *connection);
        if (register_status.IsFailed())
        {
            connection->ResetTransport();
            endpoint->QueueTerminalError(ConnectionHandle { }, NetworkStatus::TransportError, "[EpollBackend] Failed to register accepted connection.");
            return;
        }

        DeliveryEvent event { };
        event.type = DeliveryType::Connection;
        event.endpoint_id = endpoint->id;
        event.connection = connection->Handle();
        event.connection_event_type = ConnectionEventType::Connected;
        event.connection_state = ConnectionState::Connected;
        network_core.QueueDelivery(*endpoint, event);
    }
}

void EpollWorker::HandleConnection(NetworkCore& network_core, int32 fd, uint32 events)
{
    LockGuard<Mutex> lock(network_core.mutex);

    auto binding_it = fd_bindings.find(fd);
    if (binding_it == fd_bindings.end())
    {
        return;
    }

    Endpoint* endpoint = network_core.FindEndpoint(binding_it->second.endpoint_id);
    if (endpoint == nullptr || binding_it->second.slot >= endpoint->connections.size())
    {
        return;
    }

    ConnectionSlot& connection = endpoint->connections[binding_it->second.slot];
    if (connection.generation != binding_it->second.generation || connection.fd != fd)
    {
        return;
    }

    if ((events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0)
    {
        FailConnection(network_core, *endpoint, connection, NetworkStatus::TransportError, "[EpollBackend] Connection transport error.");
        return;
    }

    if (connection.state == ConnectionState::Connecting && (events & EPOLLOUT) != 0)
    {
        int32 socket_error = 0;
        socklen_t length = sizeof(socket_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &length) != 0 || socket_error != 0)
        {
            FailConnection(network_core, *endpoint, connection, NetworkStatus::TransportError, "[EpollBackend] Client connect failed.");
            return;
        }

        connection.state = ConnectionState::Connected;
        if (endpoint->client != nullptr)
        {
            endpoint->client_state = ClientState::Connected;
        }

        DeliveryEvent event { };
        event.type = DeliveryType::Connection;
        event.endpoint_id = endpoint->id;
        event.connection = connection.Handle();
        event.connection_event_type = ConnectionEventType::Connected;
        event.connection_state = ConnectionState::Connected;
        network_core.QueueDelivery(*endpoint, event);
    }

    if ((events & EPOLLIN) != 0)
    {
        HandleReadable(network_core, *endpoint, connection);
    }

    if ((events & EPOLLOUT) != 0)
    {
        HandleWritable(network_core, connection);
    }
}

void EpollWorker::HandleReadable(NetworkCore& network_core, Endpoint& endpoint, ConnectionSlot& connection)
{
    while (connection.stream_size < connection.stream_buffer.size())
    {
        size_t remaining = connection.stream_buffer.size() - connection.stream_size;
        ssize_t read_size = recv(connection.fd, connection.stream_buffer.data() + connection.stream_size, remaining, 0);
        if (read_size == 0)
        {
            CloseConnection(network_core, endpoint, connection, ConnectionEventType::Disconnected, NetworkStatus::TransportError, nullptr);
            return;
        }

        if (read_size < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }

            FailConnection(network_core, endpoint, connection, NetworkStatus::TransportError, "[EpollBackend] Receive failed.");
            return;
        }

        connection.stream_size += static_cast<size_t>(read_size);

        while (true)
        {
            PacketAssemblyResult result = TryAssembleTcpPacket(connection);
            if (result.type == PacketAssemblyResultType::NeedMoreData)
            {
                break;
            }

            if (result.type == PacketAssemblyResultType::Failed)
            {
                FailConnection(network_core, endpoint, connection, result.status, result.detail);
                return;
            }

            DeliveryEvent event { };
            event.type = DeliveryType::Message;
            event.endpoint_id = endpoint.id;
            event.connection = connection.Handle();
            event.packet_slot = result.packet_slot;
            if (!network_core.QueueDelivery(endpoint, event))
            {
                connection.ReleaseReceiveSlot(result.packet_slot);
                FailConnection(network_core, endpoint, connection, NetworkStatus::ResourceExhausted, "[EpollBackend] Delivery queue is full.");
                return;
            }
        }
    }

    if (connection.stream_size == connection.stream_buffer.size())
    {
        FailConnection(network_core, endpoint, connection, NetworkStatus::CodecError, "[EpollBackend] Receive buffer is full before packet completion.");
    }
}

void EpollWorker::HandleWritable(NetworkCore& network_core, ConnectionSlot& connection)
{
    (void)network_core;

    while (connection.active_send_slot != INVALID_SLOT)
    {
        PacketStorage& packet = connection.send_slots[connection.active_send_slot];
        const byte* data = packet.bytes.data();
        size_t remaining = static_cast<size_t>(packet.size) - connection.active_send_offset;
        ssize_t write_size = send(connection.fd, data + connection.active_send_offset, remaining, MSG_NOSIGNAL);
        if (write_size < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }

            if (connection.endpoint != nullptr)
            {
                FailConnection(network_core, *connection.endpoint, connection, NetworkStatus::TransportError, "[EpollBackend] Send failed.");
            }
            return;
        }

        connection.active_send_offset += static_cast<size_t>(write_size);
        if (connection.active_send_offset >= packet.size)
        {
            uint32 completed_slot = connection.active_send_slot;
            connection.active_send_slot = INVALID_SLOT;
            connection.active_send_offset = 0;
            connection.ReleaseSendSlot(completed_slot);

            uint32 next_slot = INVALID_SLOT;
            if (connection.pending_send_slots.Dequeue(next_slot))
            {
                connection.active_send_slot = next_slot;
                connection.active_send_offset = 0;
            }
        }
    }
}

void EpollWorker::FailConnection(NetworkCore& network_core, Endpoint& endpoint, ConnectionSlot& connection, NetworkStatus status, const char* detail)
{
    connection.state = ConnectionState::Failed;

    DeliveryEvent error_event { };
    error_event.type = DeliveryType::Error;
    error_event.endpoint_id = endpoint.id;
    error_event.connection = connection.Handle();
    error_event.status = status;
    error_event.detail = detail;
    network_core.QueueDelivery(endpoint, error_event);

    CloseConnection(network_core, endpoint, connection, ConnectionEventType::Failed, status, detail);
}

void EpollWorker::CloseConnection(NetworkCore& network_core, Endpoint& endpoint, ConnectionSlot& connection, ConnectionEventType event_type, NetworkStatus status, const char* detail)
{
    (void)status;
    (void)detail;

    ConnectionHandle handle = connection.Handle();
    if (connection.fd != INVALID_FD)
    {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connection.fd, nullptr);
        fd_bindings.erase(connection.fd);
    }

    connection.ResetTransport();

    if (endpoint.client != nullptr)
    {
        endpoint.client_state = ClientState::Disconnected;
    }

    DeliveryEvent event { };
    event.type = DeliveryType::Connection;
    event.endpoint_id = endpoint.id;
    event.connection = handle;
    event.connection_event_type = event_type;
    event.connection_state = event_type == ConnectionEventType::Failed ? ConnectionState::Failed : ConnectionState::Closed;
    network_core.QueueDelivery(endpoint, event);
}

Status<void> EpollWorker::RegisterConnectionSocket(Endpoint& endpoint, ConnectionSlot& connection)
{
    epoll_event event { };
    event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
    event.data.fd = connection.fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connection.fd, &event) != 0)
    {
        return NetworkError(NetworkStatus::TransportError, "[EpollBackend] Failed to register connection socket.");
    }

    fd_bindings[connection.fd] = FdBinding { FdKind::Connection, endpoint.id, connection.slot, connection.generation };
    return Status<void>::Success();
}

void EpollWorker::StartClientConnect(NetworkCore& network_core, Endpoint& endpoint)
{
    if (endpoint.kind != EndpointKind::Client || endpoint.connections.empty())
    {
        return;
    }

    if (endpoint.client != nullptr && endpoint.client_state != ClientState::Connecting)
    {
        return;
    }

    ConnectionSlot& connection = endpoint.connections[0];
    if (connection.state != ConnectionState::Closed && connection.state != ConnectionState::Failed)
    {
        return;
    }

    bool is_pending = false;
    Status<int32> fd_status = OpenClientSocket(endpoint.address, is_pending);
    if (fd_status.IsFailed())
    {
        if (endpoint.client != nullptr)
        {
            endpoint.client_state = ClientState::Failed;
        }
        endpoint.QueueTerminalError(ConnectionHandle { }, NetworkStatus::TransportError, fd_status.Message());
        return;
    }

    ++connection.generation;
    if (connection.generation == 0)
    {
        connection.generation = 1;
    }

    connection.fd = fd_status.Value();
    connection.state = is_pending ? ConnectionState::Connecting : ConnectionState::Connected;
    connection.stream_size = 0;
    connection.active_send_slot = INVALID_SLOT;
    connection.active_send_offset = 0;

    Status<void> register_status = RegisterConnectionSocket(endpoint, connection);
    if (register_status.IsFailed())
    {
        connection.ResetTransport();
        if (endpoint.client != nullptr)
        {
            endpoint.client_state = ClientState::Failed;
        }
        endpoint.QueueTerminalError(ConnectionHandle { }, NetworkStatus::TransportError, register_status.Message());
        return;
    }

    if (!is_pending)
    {
        if (endpoint.client != nullptr)
        {
            endpoint.client_state = ClientState::Connected;
        }

        DeliveryEvent event { };
        event.type = DeliveryType::Connection;
        event.endpoint_id = endpoint.id;
        event.connection = connection.Handle();
        event.connection_event_type = ConnectionEventType::Connected;
        event.connection_state = ConnectionState::Connected;
        network_core.QueueDelivery(endpoint, event);
    }

    return;
}

#endif

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
