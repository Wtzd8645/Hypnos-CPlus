#pragma once

#include "NetworkRuntimeDefs.hpp"

#include <Hypnos/Network/ConnectionHandle.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

enum class DeliveryType : uint8
{
    Message,
    Connection,
    Error
};

struct DeliveryEvent
{
    DeliveryType type = DeliveryType::Error;
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    ConnectionHandle connection;
    ConnectionEventType connection_event_type = ConnectionEventType::Failed;
    ConnectionState connection_state = ConnectionState::Failed;
    NetworkStatus status = NetworkStatus::TransportError;
    const char* detail = nullptr;
    uint32 packet_slot = INVALID_SLOT;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
