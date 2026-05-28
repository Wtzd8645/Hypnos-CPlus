#include "PacketFraming.hpp"

#include <cassert>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

void WritePacketHeader(PacketSize payload_size, uint8 codec_id, byte* buffer, uint32 capacity)
{
    assert(buffer != nullptr);
    assert(capacity >= PACKET_HEADER_SIZE);
    (void)capacity;

    buffer[0] = static_cast<byte>((payload_size >> 8) & 0xFF);
    buffer[1] = static_cast<byte>(payload_size & 0xFF);
    buffer[2] = static_cast<byte>(codec_id);
    buffer[3] = static_cast<byte>(0);
}

Status<PacketHeader> ReadPacketHeader(const byte* buffer, uint32 size)
{
    if (buffer == nullptr || size < PACKET_HEADER_SIZE)
    {
        return Status<PacketHeader>::Error(ToErrorCode(NetworkStatus::CodecError), "[PacketFraming] Packet header is incomplete.");
    }

    PacketHeader header { };
    header.payload_size = static_cast<PacketSize>(
        (static_cast<uint16>(static_cast<uint8>(buffer[0])) << 8)
        | static_cast<uint16>(static_cast<uint8>(buffer[1])));
    header.codec_id = static_cast<uint8>(buffer[2]);
    header.flags = static_cast<uint8>(buffer[3]);
    return Status<PacketHeader>::Success(header);
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
