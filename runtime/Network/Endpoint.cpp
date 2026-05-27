#include "NetworkCore.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

Status<void> Endpoint::Register(MessageHandler handler)
{
    Status<void> status = CheckRegistrationAllowed();
    if (status.IsFailed())
    {
        return status;
    }

    message_handler = handler;
    return Status<void>::Success();
}

Status<void> Endpoint::Register(ConnectionEventHandler handler)
{
    Status<void> status = CheckRegistrationAllowed();
    if (status.IsFailed())
    {
        return status;
    }

    connection_event_handler = handler;
    return Status<void>::Success();
}

Status<void> Endpoint::Register(ErrorHandler handler)
{
    Status<void> status = CheckRegistrationAllowed();
    if (status.IsFailed())
    {
        return status;
    }

    error_handler = handler;
    return Status<void>::Success();
}

Status<void> Endpoint::Send(ConnectionHandle connection, IMessage& message)
{
    if (owner == nullptr)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Endpoint is not bound.");
    }

    if (kind != EndpointKind::Server)
    {
        return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Server send requires a server endpoint.");
    }

    if (owner->is_dispatching_message_callback)
    {
        return NetworkError(NetworkStatus::InvalidState, "[Endpoint] Message callback reentrant send is not supported.");
    }

    if (owner->manager_state != ManagerState::Running)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Server send requires a running manager.");
    }

    return QueueMessage(connection, message);
}

Status<void> Endpoint::Send(IMessage& message)
{
    if (owner == nullptr)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Endpoint is not bound.");
    }

    if (kind != EndpointKind::Client)
    {
        return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Client send requires a client endpoint.");
    }

    if (owner->is_dispatching_message_callback)
    {
        return NetworkError(NetworkStatus::InvalidState, "[Endpoint] Message callback reentrant send is not supported.");
    }

    if (owner->manager_state != ManagerState::Running)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Client send requires a running manager.");
    }

    ConnectionHandle connection;
    {
        LockGuard<Mutex> lock(owner->mutex);
        if (client_state != ClientState::Connected)
        {
            return NetworkError(NetworkStatus::InvalidState, "[Endpoint] Client send requires a connected endpoint.");
        }

        if (connections.empty())
        {
            return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Client connection does not exist.");
        }

        connection = connections[0].Handle();
    }

    return QueueMessage(connection, message);
}

Status<void> Endpoint::Close(ConnectionHandle connection_handle)
{
    if (owner == nullptr)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Endpoint is not bound.");
    }

    if (kind != EndpointKind::Server)
    {
        return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Server close requires a server endpoint.");
    }

    if (owner->is_dispatching_message_callback)
    {
        return NetworkError(NetworkStatus::InvalidState, "[Endpoint] Message callback reentrant close is not supported.");
    }

    if (owner->manager_state != ManagerState::Running)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Server close requires a running manager.");
    }

    LockGuard<Mutex> lock(owner->mutex);

    ConnectionSlot* connection = Resolve(connection_handle);
    if (connection == nullptr)
    {
        return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Close target handle is invalid.");
    }

    if (connection->state == ConnectionState::Closed)
    {
        return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Close target is closed.");
    }

    ConnectionState previous_state = connection->state;
    if (connection->state == ConnectionState::Connected)
    {
        connection->state = ConnectionState::Closing;
    }

#if defined(__linux__)
    WorkerCommand command { };
    command.type = WorkerCommandType::Close;
    command.endpoint_id = id;
    command.connection = connection_handle;
    if (!owner->workers[worker_id]->EnqueueCommand(command))
    {
        connection->state = previous_state;
        return NetworkError(NetworkStatus::Busy, "[Endpoint] Worker command queue is full.");
    }
    return Status<void>::Success();
#else
    connection->state = previous_state;
    return NetworkError(NetworkStatus::Unsupported, "[Endpoint] Epoll backend requires Linux.");
#endif
}

Status<void> Endpoint::Connect()
{
    if (owner == nullptr)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Endpoint is not bound.");
    }

    if (kind != EndpointKind::Client)
    {
        return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Connect requires a client endpoint.");
    }

    if (owner->manager_state != ManagerState::Running)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Client connect requires a running manager.");
    }

    LockGuard<Mutex> lock(owner->mutex);

    if (client_state == ClientState::Connecting || client_state == ClientState::Connected)
    {
        return NetworkError(NetworkStatus::InvalidState, "[Endpoint] Client is already connecting or connected.");
    }

    ClientState previous_state = client_state;
    client_state = ClientState::Connecting;

#if defined(__linux__)
    WorkerCommand command { };
    command.type = WorkerCommandType::ConnectClient;
    command.endpoint_id = id;
    if (!owner->workers[worker_id]->EnqueueCommand(command))
    {
        client_state = previous_state;
        return NetworkError(NetworkStatus::Busy, "[Endpoint] Worker command queue is full.");
    }
    return Status<void>::Success();
#else
    client_state = previous_state;
    return NetworkError(NetworkStatus::Unsupported, "[Endpoint] Epoll backend requires Linux.");
#endif
}

Status<void> Endpoint::Disconnect()
{
    if (owner == nullptr)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Endpoint is not bound.");
    }

    if (kind != EndpointKind::Client)
    {
        return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Disconnect requires a client endpoint.");
    }

    if (owner->manager_state != ManagerState::Running)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Client disconnect requires a running manager.");
    }

    LockGuard<Mutex> lock(owner->mutex);

    if (client_state == ClientState::Disconnected)
    {
        return Status<void>::Success();
    }

    if (client_state != ClientState::Connecting && client_state != ClientState::Connected)
    {
        return NetworkError(NetworkStatus::InvalidState, "[Endpoint] Client disconnect is not valid in the current state.");
    }

    ConnectionSlot& connection = connections[0];
    if (connection.state == ConnectionState::Closed || connection.state == ConnectionState::Failed)
    {
        client_state = ClientState::Disconnected;
        return Status<void>::Success();
    }

    ClientState previous_state = client_state;
    client_state = ClientState::Disconnecting;

#if defined(__linux__)
    WorkerCommand command { };
    command.type = WorkerCommandType::DisconnectClient;
    command.endpoint_id = id;
    command.connection = connection.Handle();
    if (!owner->workers[worker_id]->EnqueueCommand(command))
    {
        client_state = previous_state;
        return NetworkError(NetworkStatus::Busy, "[Endpoint] Worker command queue is full.");
    }
    return Status<void>::Success();
#else
    client_state = previous_state;
    return NetworkError(NetworkStatus::Unsupported, "[Endpoint] Epoll backend requires Linux.");
#endif
}

Status<void> Endpoint::CheckRegistrationAllowed() const
{
    if (owner == nullptr)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Endpoint is not bound.");
    }

    if (owner->manager_state != ManagerState::Configured && owner->manager_state != ManagerState::Running)
    {
        return NetworkError(NetworkStatus::InvalidState, "[Endpoint] Register requires configured or running state.");
    }

    if (owner->is_dispatching_callbacks)
    {
        return NetworkError(NetworkStatus::InvalidState, "[Endpoint] Register is not allowed during callback dispatch.");
    }

    return Status<void>::Success();
}

Status<void> Endpoint::QueueMessage(ConnectionHandle connection_handle, IMessage& message)
{
    if (owner == nullptr)
    {
        return NetworkError(NetworkStatus::NotReady, "[Endpoint] Endpoint is not bound.");
    }

    LockGuard<Mutex> lock(owner->mutex);

    ConnectionSlot* connection = Resolve(connection_handle);
    if (connection == nullptr)
    {
        return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Send target handle is invalid.");
    }

    if (connection->state == ConnectionState::Closing)
    {
        return NetworkError(NetworkStatus::InvalidState, "[Endpoint] Send target is closing.");
    }

    if (connection->state != ConnectionState::Connected)
    {
        return NetworkError(NetworkStatus::InvalidHandle, "[Endpoint] Send target is not connected.");
    }

    ICodec* codec = owner->config.owned_objects.codec_registry->Find(message.CodecId());
    if (codec == nullptr)
    {
        return NetworkError(NetworkStatus::CodecError, "[Endpoint] Message codec id is not registered.");
    }

    uint32 packet_slot = connection->AcquireSendSlot();
    if (packet_slot == INVALID_SLOT)
    {
        return NetworkError(NetworkStatus::Busy, "[Endpoint] Connection send queue is full.");
    }

    PacketStorage& packet = connection->send_slots[packet_slot];
    byte* payload = packet.bytes.data() + PACKET_HEADER_SIZE;
    PacketSize payload_capacity = static_cast<PacketSize>(packet.bytes.size() - PACKET_HEADER_SIZE);
    Status<PacketSize> encode_status = owner->config.owned_objects.packet_pipeline->Encode(*codec, message, payload, payload_capacity);
    if (encode_status.IsFailed())
    {
        connection->ReleaseSendSlot(packet_slot);
        return Status<void>::Error(static_cast<ErrorCode>(encode_status.ErrorCode()), encode_status.Message());
    }

    Status<void> header_status = WritePacketHeader(encode_status.Value(), codec->Id(), packet.bytes.data(), static_cast<uint32>(packet.bytes.size()));
    if (header_status.IsFailed())
    {
        connection->ReleaseSendSlot(packet_slot);
        return header_status;
    }

    packet.codec_id = codec->Id();
    packet.size = static_cast<uint32>(PACKET_HEADER_SIZE + encode_status.Value());

#if defined(__linux__)
    WorkerCommand command { };
    command.type = WorkerCommandType::Send;
    command.endpoint_id = id;
    command.connection = connection_handle;
    command.packet_slot = packet_slot;
    if (!owner->workers[worker_id]->EnqueueCommand(command))
    {
        connection->ReleaseSendSlot(packet_slot);
        return NetworkError(NetworkStatus::Busy, "[Endpoint] Worker command queue is full.");
    }
#else
    connection->ReleaseSendSlot(packet_slot);
    return NetworkError(NetworkStatus::Unsupported, "[Endpoint] Epoll backend requires Linux.");
#endif

    return Status<void>::Success();
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
