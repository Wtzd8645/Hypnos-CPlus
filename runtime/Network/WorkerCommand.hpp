#pragma once

#include "NetworkRuntimeDefs.hpp"

#include <Hypnos/Network/ConnectionHandle.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

enum class WorkerCommandType : uint8
{
    ConnectClient,
    DisconnectClient,
    Send,
    Close,
    Shutdown
};

struct WorkerCommand
{
    WorkerCommandType type = WorkerCommandType::Shutdown;
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    ConnectionHandle connection;
    uint32 packet_slot = INVALID_SLOT;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
