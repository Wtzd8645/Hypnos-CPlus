#pragma once

#include "InboundPacketProcessor.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct DeliveryEvent;
struct NetworkCore;

void DispatchConnectionCallback(NetworkCore& network_core, const DeliveryEvent& event);
void DispatchErrorCallback(NetworkCore& network_core, const DeliveryEvent& event);
void DispatchInboundPacketCallback(NetworkCore& network_core, const InboundPacketResult& result);

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
