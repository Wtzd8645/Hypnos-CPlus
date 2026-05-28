#include "DeliveryPump.hpp"

#include "InboundPacketProcessor.hpp"
#include "NetworkCore.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {
namespace {

class DeliveryPump
{
public:
    explicit DeliveryPump(NetworkCore& network_core) :
        network_core(network_core)
    {
    }

    void Run()
    {
        while (true)
        {
            DeliveryEvent event { };
            if (!TryDequeueDelivery(event))
            {
                return;
            }

            if (event.type == DeliveryType::Connection)
            {
                DispatchConnection(event);
                continue;
            }

            if (event.type == DeliveryType::Error)
            {
                DispatchError(event);
                continue;
            }

            InboundPacketResult result { };
            ProcessInboundPacket(network_core, event, result);
            DispatchInboundPacket(result);

            if (result.should_close_after_error)
            {
                EnqueueCloseAfterError(event, result.close_worker_id);
            }
        }
    }

private:
    bool TryDequeueTerminalError(DeliveryEvent& event)
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

    bool TryDequeueWorkerDelivery(DeliveryEvent& event)
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
        (void)event;
#endif

        return false;
    }

    bool TryDequeueDelivery(DeliveryEvent& event)
    {
        return TryDequeueTerminalError(event) || TryDequeueWorkerDelivery(event);
    }

    void DispatchConnection(const DeliveryEvent& event)
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

    void DispatchError(const DeliveryEvent& event)
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

    void DispatchInboundPacket(const InboundPacketResult& result)
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

    void EnqueueCloseAfterError(const DeliveryEvent& event, uint16 worker_id)
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
        (void)event;
        (void)worker_id;
#endif
    }

    NetworkCore& network_core;
};

} // namespace

void RunDeliveryPump(NetworkCore& network_core)
{
    DeliveryPump pump(network_core);
    pump.Run();
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
