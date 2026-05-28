#include "NetworkCore.hpp"

#include "SocketUtils_Unix.hpp"

#include <memory>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

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

void NetworkCore::DispatchCallbacks(NetworkManager& manager)
{
    (void)manager;

    while (true)
    {
        DeliveryEvent event { };
        bool has_event = false;

        {
            LockGuard<Mutex> lock(mutex);
            for (UniquePtr<Endpoint>& endpoint : endpoints)
            {
                if (endpoint->has_pending_terminal_error)
                {
                    event = endpoint->terminal_error;
                    endpoint->has_pending_terminal_error = false;
                    has_event = true;
                    break;
                }

            }
        }

#if defined(__linux__)
        if (!has_event)
        {
            for (UniquePtr<EpollWorker>& worker : workers)
            {
                if (worker->delivery_queue != nullptr && worker->delivery_queue->Dequeue(event))
                {
                    has_event = true;
                    break;
                }
            }
        }
#endif

        if (!has_event)
        {
            break;
        }

        if (event.type == DeliveryType::Connection)
        {
            ConnectionEventHandler handler;
            {
                LockGuard<Mutex> lock(mutex);
                Endpoint* endpoint = FindEndpoint(event.endpoint_id);
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
            continue;
        }

        if (event.type == DeliveryType::Error)
        {
            ErrorHandler handler;
            {
                LockGuard<Mutex> lock(mutex);
                Endpoint* endpoint = FindEndpoint(event.endpoint_id);
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
            continue;
        }

        MessageHandler message_handler;
        ErrorHandler error_handler;
        IMessage* message = nullptr;
        NetworkErrorEvent error_event { };
        bool close_after_error = false;
        uint16 close_worker_id = 0;

        {
            LockGuard<Mutex> lock(mutex);
            Endpoint* endpoint = FindEndpoint(event.endpoint_id);
            if (endpoint == nullptr)
            {
                continue;
            }

            message_handler = endpoint->message_handler;
            error_handler = endpoint->error_handler;

            ConnectionSlot* connection = endpoint->Resolve(event.connection);
            if (connection == nullptr || event.packet_slot >= connection->receive_slots.size())
            {
                continue;
            }

            PacketStorage& packet = connection->receive_slots[event.packet_slot];
            Status<PacketHeader> header_status = ReadPacketHeader(packet.bytes.data(), packet.size);
            if (header_status.IsFailed())
            {
                connection->ReleaseReceiveSlot(event.packet_slot);
                continue;
            }

            PacketHeader header = header_status.Value();
            ICodec* codec = config.owned_objects.codec_registry->Find(header.codec_id);
            if (codec == nullptr)
            {
                connection->ReleaseReceiveSlot(event.packet_slot);
                connection->state = ConnectionState::Failed;
                error_event = NetworkErrorEvent { event.endpoint_id, true, event.connection, NetworkStatus::CodecError, "[NetworkManager] Received packet codec id is not registered." };
                close_after_error = true;
                close_worker_id = endpoint->worker_id;
            }
            else
            {
                const byte* payload = packet.bytes.data() + PACKET_HEADER_SIZE;
                Status<IMessage*> decode_status = config.owned_objects.packet_pipeline->Decode(
                    *codec,
                    payload,
                    header.payload_size,
                    *config.owned_objects.message_allocator);

                connection->ReleaseReceiveSlot(event.packet_slot);
                if (decode_status.IsFailed() || decode_status.Value() == nullptr)
                {
                    connection->state = ConnectionState::Failed;
                    error_event = NetworkErrorEvent { event.endpoint_id, true, event.connection, NetworkStatus::CodecError, decode_status.IsFailed() ? decode_status.Message() : "[NetworkManager] Decode returned null message." };
                    close_after_error = true;
                    close_worker_id = endpoint->worker_id;
                }
                else
                {
                    message = decode_status.Value();
                    delivered_messages.push_back(message);
                }
            }
        }

        if (message != nullptr && message_handler)
        {
            MessageEvent message_event { event.endpoint_id, event.connection, *message };
            is_dispatching_message_callback = true;
            message_handler(message_event);
            is_dispatching_message_callback = false;
        }
        else if (message == nullptr && error_handler)
        {
            error_handler(error_event);
        }

#if defined(__linux__)
        if (close_after_error && close_worker_id < workers.size())
        {
            WorkerCommand command { };
            command.type = WorkerCommandType::Close;
            command.endpoint_id = event.endpoint_id;
            command.connection = event.connection;
            (void)workers[close_worker_id]->EnqueueCommand(command);
        }
#endif
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
