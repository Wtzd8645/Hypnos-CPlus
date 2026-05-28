#include "InboundPacketProcessor.hpp"

#include "NetworkCore.hpp"

#include <cassert>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

static void FailPacket(
    InboundPacketResult& result,
    ConnectionSlot& connection,
    uint32 packet_slot,
    uint16 worker_id,
    NetworkStatus status,
    const char* detail)
{
    connection.ReleaseReceiveSlot(packet_slot);
    connection.state = ConnectionState::Failed;
    result.error_event = NetworkErrorEvent { result.endpoint_id, true, result.connection, status, detail };
    result.has_error = true;
    result.should_close_after_error = true;
    result.close_worker_id = worker_id;
}

void ProcessInboundPacket(NetworkCore& network_core, const DeliveryEvent& event, InboundPacketResult& result)
{
    result.endpoint_id = event.endpoint_id;
    result.connection = event.connection;

    LockGuard<Mutex> lock(network_core.mutex);
    Endpoint* endpoint = network_core.FindEndpoint(event.endpoint_id);
    if (endpoint == nullptr)
    {
        return;
    }

    result.message_handler = endpoint->message_handler;
    result.error_handler = endpoint->error_handler;

    ConnectionSlot* connection = endpoint->Resolve(event.connection);
    if (connection == nullptr || event.packet_slot >= connection->receive_slots.size())
    {
        return;
    }

    PacketStorage& packet = connection->receive_slots[event.packet_slot];
    if (packet.size < PACKET_HEADER_SIZE)
    {
        assert(false && "[InboundPacketProcessor] Packet storage must contain a complete packet.");
        FailPacket(
            result,
            *connection,
            event.packet_slot,
            endpoint->worker_id,
            NetworkStatus::CodecError,
            "[NetworkManager] Received packet storage is incomplete.");
        return;
    }

    ICodec* codec = packet.codec_id < network_core.config.codecs.size()
        ? network_core.config.codecs[packet.codec_id].get()
        : nullptr;
    if (codec == nullptr)
    {
        FailPacket(
            result,
            *connection,
            event.packet_slot,
            endpoint->worker_id,
            NetworkStatus::CodecError,
            "[NetworkManager] Received packet codec id is not registered.");
        return;
    }

    assert(codec->Id() == packet.codec_id);

    const byte* payload = packet.bytes.data() + PACKET_HEADER_SIZE;
    PacketSize payload_size = static_cast<PacketSize>(packet.size - PACKET_HEADER_SIZE);
    Status<IMessage*> decode_status = network_core.config.packet_pipeline->Decode(
        *codec,
        payload,
        payload_size,
        *network_core.config.message_allocator);

    connection->ReleaseReceiveSlot(event.packet_slot);
    if (decode_status.IsFailed() || decode_status.Value() == nullptr)
    {
        connection->state = ConnectionState::Failed;
        result.error_event = NetworkErrorEvent {
            event.endpoint_id,
            true,
            event.connection,
            NetworkStatus::CodecError,
            decode_status.IsFailed() ? decode_status.Message() : "[NetworkManager] Decode returned null message."
        };
        result.has_error = true;
        result.should_close_after_error = true;
        result.close_worker_id = endpoint->worker_id;
        return;
    }

    result.message = decode_status.Value();
    network_core.delivered_messages.push_back(result.message);
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
