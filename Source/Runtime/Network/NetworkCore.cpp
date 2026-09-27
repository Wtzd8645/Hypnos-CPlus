#include "NetworkCore.hpp"

#include "SocketUtils_Unix.hpp"

#include <cassert>
#include <memory>

namespace Blanketmen {
namespace Hypnos {
namespace Network {
namespace {

struct InboundMessageResult
{
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    ConnectionHandle connection;
    MessageHandler message_handler;
    ErrorHandler error_handler;
    IMessage* message = nullptr;
    NetworkErrorEvent error_event { };
    bool has_error = false;
    bool should_close_after_error = false;
    uint16 close_worker_id = 0;
};

bool TryDequeueTerminalError(NetworkCore& network_core, DeliveryEvent& event)
{
    LockGuard<Mutex> lock(network_core.mutex);
    for (UniquePtr<Endpoint>& endpoint : network_core.endpoints)
    {
        if (endpoint != nullptr && endpoint->has_pending_terminal_error)
        {
            event = endpoint->terminal_error;
            endpoint->has_pending_terminal_error = false;
            return true;
        }
    }

    return false;
}

bool TryDequeueWorkerDelivery(NetworkCore& network_core, DeliveryEvent& event)
{
#if defined(__linux__)
    for (UniquePtr<EpollWorker>& worker : network_core.workers)
    {
        if (worker != nullptr && worker->delivery_queue != nullptr && worker->delivery_queue->Dequeue(event))
        {
            return true;
        }
    }
#else
    (void)network_core;
    (void)event;
#endif

    return false;
}

bool TryDequeueDelivery(NetworkCore& network_core, DeliveryEvent& event)
{
    return TryDequeueTerminalError(network_core, event) || TryDequeueWorkerDelivery(network_core, event);
}

void DispatchConnection(NetworkCore& network_core, const DeliveryEvent& event)
{
    ConnectionEventHandler handler;
    {
        LockGuard<Mutex> lock(network_core.mutex);
        Endpoint* endpoint = network_core.FindEndpoint(event.endpoint_id);
        if (endpoint != nullptr)
        {
            handler = endpoint->connection_event_handler;
        }
    }

    if (handler)
    {
        ConnectionEvent connection_event { event.endpoint_id, event.connection, event.connection_event_type, event.connection_state };
        handler(connection_event);
    }
}

void DispatchError(NetworkCore& network_core, const DeliveryEvent& event)
{
    ErrorHandler handler;
    {
        LockGuard<Mutex> lock(network_core.mutex);
        Endpoint* endpoint = network_core.FindEndpoint(event.endpoint_id);
        if (endpoint != nullptr)
        {
            handler = endpoint->error_handler;
        }
    }

    if (handler)
    {
        NetworkErrorEvent error_event { };
        error_event.endpoint_id = event.endpoint_id;
        error_event.has_connection = event.connection.IsValid();
        error_event.connection = event.connection;
        error_event.status = event.status;
        error_event.detail = event.detail;
        handler(error_event);
    }
}

void FailInboundMessage(
    InboundMessageResult& result,
    ConnectionSlot& connection,
    uint32 packet_slot,
    uint16 worker_id,
    NetworkStatus status,
    const char* detail)
{
    connection.ReleaseReceiveSlot(packet_slot);
    connection.state = ConnectionState::Failed;
    result.error_event = NetworkErrorEvent { result.endpoint_id, true, result.connection, status, detail };
    result.has_error = true;
    result.should_close_after_error = true;
    result.close_worker_id = worker_id;
}

void MaterializeInboundMessage(NetworkCore& network_core, const DeliveryEvent& event, InboundMessageResult& result)
{
    result.endpoint_id = event.endpoint_id;
    result.connection = event.connection;

    LockGuard<Mutex> lock(network_core.mutex);
    Endpoint* endpoint = network_core.FindEndpoint(event.endpoint_id);
    if (endpoint == nullptr)
    {
        return;
    }

    result.message_handler = endpoint->message_handler;
    result.error_handler = endpoint->error_handler;

    ConnectionSlot* connection = endpoint->Resolve(event.connection);
    if (connection == nullptr || event.packet_slot >= connection->receive_slots.size())
    {
        return;
    }

    PacketStorage& packet = connection->receive_slots[event.packet_slot];
    if (packet.size < PACKET_HEADER_SIZE)
    {
        assert(false && "[NetworkCore] Packet storage must contain a complete packet.");
        FailInboundMessage(
            result,
            *connection,
            event.packet_slot,
            endpoint->worker_id,
            NetworkStatus::CodecError,
            "[NetworkManager] Received packet storage is incomplete.");
        return;
    }

    ICodec* codec = packet.codec_id < network_core.config.codecs.size()
        ? network_core.config.codecs[packet.codec_id].get()
        : nullptr;
    if (codec == nullptr)
    {
        FailInboundMessage(
            result,
            *connection,
            event.packet_slot,
            endpoint->worker_id,
            NetworkStatus::CodecError,
            "[NetworkManager] Received packet codec id is not registered.");
        return;
    }

    assert(codec->Id() == packet.codec_id);

    const byte* payload = packet.bytes.data() + PACKET_HEADER_SIZE;
    PacketSize payload_size = static_cast<PacketSize>(packet.size - PACKET_HEADER_SIZE);
    Status<IMessage*> decode_status = network_core.config.packet_pipeline->Decode(
        *codec,
        payload,
        payload_size,
        *network_core.config.message_allocator);

    connection->ReleaseReceiveSlot(event.packet_slot);
    if (decode_status.IsFailed() || decode_status.Value() == nullptr)
    {
        connection->state = ConnectionState::Failed;
        result.error_event = NetworkErrorEvent {
            event.endpoint_id,
            true,
            event.connection,
            NetworkStatus::CodecError,
            decode_status.IsFailed() ? decode_status.Message() : "[NetworkManager] Decode returned null message."
        };
        result.has_error = true;
        result.should_close_after_error = true;
        result.close_worker_id = endpoint->worker_id;
        return;
    }

    result.message = decode_status.Value();
    network_core.delivered_messages.push_back(result.message);
}

void DispatchInboundMessage(NetworkCore& network_core, const InboundMessageResult& result)
{
    if (result.message != nullptr && result.message_handler)
    {
        MessageEvent message_event { result.endpoint_id, result.connection, *result.message };
        network_core.is_dispatching_message_callback = true;
        result.message_handler(message_event);
        network_core.is_dispatching_message_callback = false;
        return;
    }

    if (result.has_error && result.error_handler)
    {
        result.error_handler(result.error_event);
    }
}

void EnqueueCloseAfterError(NetworkCore& network_core, const DeliveryEvent& event, uint16 worker_id)
{
#if defined(__linux__)
    if (worker_id < network_core.workers.size() && network_core.workers[worker_id] != nullptr)
    {
        WorkerCommand command { };
        command.type = WorkerCommandType::Close;
        command.endpoint_id = event.endpoint_id;
        command.connection = event.connection;
        (void)network_core.workers[worker_id]->EnqueueCommand(command);
    }
#else
    (void)network_core;
    (void)event;
    (void)worker_id;
#endif
}

} // namespace

Status<void> NetworkCore::Start()
{
#if !defined(__linux__)
    return NetworkError(NetworkStatus::Unsupported, "[NetworkManager] Epoll backend requires Linux.");
#else
    CleanupTransport();
    workers.clear();
    workers.resize(config.worker_count);

    for (uint16 i = 0; i < config.worker_count; ++i)
    {
        workers[i] = std::make_unique<EpollWorker>();
        Status<void> status = workers[i]->Initialize(i, WorkerCommandCapacity(i), WorkerDeliveryCapacity(i));
        if (status.IsFailed())
        {
            CleanupTransport();
            return status;
        }
    }

    for (UniquePtr<Endpoint>& endpoint : endpoints)
    {
        if (endpoint->kind == EndpointKind::Server)
        {
            if (endpoint->server != nullptr)
            {
                endpoint->server_state = ServerState::Starting;
            }

            Status<int32> listen_status = OpenListenSocket(endpoint->address);
            if (listen_status.IsFailed())
            {
                CleanupTransport();
                return Status<void>::Error(static_cast<ErrorCode>(listen_status.ErrorCode()), listen_status.Message());
            }

            endpoint->listen_fd = listen_status.Value();
            Status<void> add_status = workers[endpoint->worker_id]->AddListenSocket(endpoint->id, endpoint->listen_fd);
            if (add_status.IsFailed())
            {
                CleanupTransport();
                return add_status;
            }

            if (endpoint->server != nullptr)
            {
                endpoint->server_state = ServerState::Listening;
            }
        }
        else if (endpoint->client != nullptr)
        {
            endpoint->client_state = ClientState::Disconnected;
        }
    }

    for (UniquePtr<EpollWorker>& worker : workers)
    {
        worker->Start(*this);
    }

    return Status<void>::Success();
#endif
}

void NetworkCore::Stop()
{
    CleanupTransport();
}

void NetworkCore::ProcessDeliveries()
{
    while (true)
    {
        DeliveryEvent event { };
        if (!TryDequeueDelivery(*this, event))
        {
            return;
        }

        if (event.type == DeliveryType::Connection)
        {
            DispatchConnection(*this, event);
            continue;
        }

        if (event.type == DeliveryType::Error)
        {
            DispatchError(*this, event);
            continue;
        }

        InboundMessageResult result { };
        MaterializeInboundMessage(*this, event, result);
        DispatchInboundMessage(*this, result);

        if (result.should_close_after_error)
        {
            EnqueueCloseAfterError(*this, event, result.close_worker_id);
        }
    }
}

void NetworkCore::CleanupTransport()
{
    ReleaseDeliveredMessages();

#if defined(__linux__)
    for (UniquePtr<EpollWorker>& worker : workers)
    {
        if (worker != nullptr)
        {
            worker->Stop();
        }
    }
#endif

    workers.clear();

    for (UniquePtr<Endpoint>& endpoint : endpoints)
    {
        endpoint->ResetTransport();
    }
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
