#pragma once

#include <Hypnos/Network/NetworkDefs.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct PacketHeader
{
    PacketSize payload_size = 0;
    uint8 codec_id = 0;
    uint8 flags = 0;
};

void WritePacketHeader(PacketSize payload_size, uint8 codec_id, byte* buffer, uint32 capacity);
Status<PacketHeader> ReadPacketHeader(const byte* buffer, uint32 size);

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
