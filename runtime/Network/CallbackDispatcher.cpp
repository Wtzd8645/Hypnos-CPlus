#include "CallbackDispatcher.hpp"

#include "NetworkCore.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

void DispatchConnectionCallback(NetworkCore& network_core, const DeliveryEvent& event)
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

void DispatchErrorCallback(NetworkCore& network_core, const DeliveryEvent& event)
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

void DispatchInboundPacketCallback(NetworkCore& network_core, const InboundPacketResult& result)
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

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
