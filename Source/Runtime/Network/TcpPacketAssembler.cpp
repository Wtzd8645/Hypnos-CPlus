#include "TcpPacketAssembler.hpp"

#include "PacketFraming.hpp"

#include <cstring>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

static PacketAssemblyResult Failed(NetworkStatus status, const char* detail)
{
    PacketAssemblyResult result { };
    result.type = PacketAssemblyResultType::Failed;
    result.status = status;
    result.detail = detail;
    return result;
}

PacketAssemblyResult TryAssembleTcpPacket(ConnectionSlot& connection)
{
    PacketAssemblyResult result { };
    if (connection.stream_size < PACKET_HEADER_SIZE)
    {
        return result;
    }

    Status<PacketHeader> header_status = ReadPacketHeader(connection.stream_buffer.data(), static_cast<uint32>(connection.stream_size));
    if (header_status.IsFailed())
    {
        return Failed(NetworkStatus::CodecError, "[EpollBackend] Packet header decode failed.");
    }

    PacketHeader header = header_status.Value();
    size_t packet_size = static_cast<size_t>(PACKET_HEADER_SIZE) + header.payload_size;
    if (packet_size > connection.stream_buffer.size())
    {
        return Failed(NetworkStatus::CodecError, "[EpollBackend] Packet exceeds configured codec buffer capacity.");
    }

    if (header.flags != 0)
    {
        return Failed(NetworkStatus::CodecError, "[EpollBackend] Packet flags are unsupported.");
    }

    if (connection.stream_size < packet_size)
    {
        return result;
    }

    uint32 packet_slot = connection.AcquireReceiveSlot();
    if (packet_slot == INVALID_SLOT)
    {
        return Failed(NetworkStatus::ResourceExhausted, "[EpollBackend] Receive packet slots are exhausted.");
    }

    PacketStorage& packet = connection.receive_slots[packet_slot];
    std::memcpy(packet.bytes.data(), connection.stream_buffer.data(), packet_size);
    packet.size = static_cast<uint32>(packet_size);
    packet.codec_id = header.codec_id;

    size_t remaining_bytes = connection.stream_size - packet_size;
    if (remaining_bytes > 0)
    {
        std::memmove(connection.stream_buffer.data(), connection.stream_buffer.data() + packet_size, remaining_bytes);
    }
    connection.stream_size = remaining_bytes;

    result.type = PacketAssemblyResultType::PacketReady;
    result.packet_slot = packet_slot;
    return result;
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
