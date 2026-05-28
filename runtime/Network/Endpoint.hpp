#pragma once

#include "ConnectionSlot.hpp"
#include "DeliveryEvent.hpp"

#include <Hypnos/Network/Client.hpp>
#include <Hypnos/Network/NetworkConfig.hpp>
#include <Hypnos/Network/Server.hpp>

#include <cassert>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct NetworkCore;

enum class EndpointKind : uint8
{
    Server,
    Client
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

    void InitializeServer(NetworkCore& network_core, Server& server_ref, const ServerConfig& config)
    {
        assert(config.id != INVALID_ENDPOINT_ID);
        assert(config.max_connections > 0);
        assert(config.send_queue_capacity_per_connection > 0);
        assert(config.receive_queue_capacity_per_connection > 0);
        assert(config.codec_buffer_capacity > 0);

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
            connections[i].Initialize(
                this,
                i,
                config.send_queue_capacity_per_connection,
                config.receive_queue_capacity_per_connection,
                config.codec_buffer_capacity);
        }
    }

    void InitializeClient(NetworkCore& network_core, Client& client_ref, const ClientConfig& config)
    {
        assert(config.id != INVALID_ENDPOINT_ID);
        assert(config.send_queue_capacity > 0);
        assert(config.receive_queue_capacity > 0);
        assert(config.codec_buffer_capacity > 0);

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
        connections[0].Initialize(
            this,
            0,
            config.send_queue_capacity,
            config.receive_queue_capacity,
            config.codec_buffer_capacity);
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

inline ConnectionHandle ConnectionSlot::Handle() const noexcept
{
    return ConnectionHandle(EndpointIdValue(), slot, generation);
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
