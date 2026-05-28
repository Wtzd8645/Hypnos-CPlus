#pragma once

#include "ConnectionSlot.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

enum class PacketAssemblyResultType : uint8
{
    NeedMoreData,
    PacketReady,
    Failed
};

struct PacketAssemblyResult
{
    PacketAssemblyResultType type = PacketAssemblyResultType::NeedMoreData;
    uint32 packet_slot = INVALID_SLOT;
    NetworkStatus status = NetworkStatus::CodecError;
    const char* detail = nullptr;
};

PacketAssemblyResult TryAssembleTcpPacket(ConnectionSlot& connection);

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
