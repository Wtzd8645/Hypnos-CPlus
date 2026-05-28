#pragma once

#include "DeliveryEvent.hpp"

#include <Hypnos/Network/Server.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct NetworkCore;

struct InboundPacketResult
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

void ProcessInboundPacket(NetworkCore& network_core, const DeliveryEvent& event, InboundPacketResult& result);

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
