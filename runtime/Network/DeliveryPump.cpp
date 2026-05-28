#include "DeliveryPump.hpp"

#include "CallbackDispatcher.hpp"
#include "NetworkCore.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

static bool TryDequeueTerminalError(NetworkCore& network_core, DeliveryEvent& event)
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

static bool TryDequeueWorkerDelivery(NetworkCore& network_core, DeliveryEvent& event)
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

static bool TryDequeueDelivery(NetworkCore& network_core, DeliveryEvent& event)
{
    return TryDequeueTerminalError(network_core, event) || TryDequeueWorkerDelivery(network_core, event);
}

static void EnqueueCloseAfterError(NetworkCore& network_core, const DeliveryEvent& event, uint16 worker_id)
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

void RunDeliveryPump(NetworkCore& network_core)
{
    while (true)
    {
        DeliveryEvent event { };
        if (!TryDequeueDelivery(network_core, event))
        {
            return;
        }

        if (event.type == DeliveryType::Connection)
        {
            DispatchConnectionCallback(network_core, event);
            continue;
        }

        if (event.type == DeliveryType::Error)
        {
            DispatchErrorCallback(network_core, event);
            continue;
        }

        InboundPacketResult result { };
        ProcessInboundPacket(network_core, event, result);
        DispatchInboundPacketCallback(network_core, result);

        if (result.should_close_after_error)
        {
            EnqueueCloseAfterError(network_core, event, result.close_worker_id);
        }
    }
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
