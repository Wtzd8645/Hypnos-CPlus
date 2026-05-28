#pragma once

#include "BoundedQueue.hpp"
#include "PacketStorage.hpp"

#include <Hypnos/Network/ConnectionHandle.hpp>

#include <cassert>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct Endpoint;

struct ConnectionSlot
{
    Endpoint* endpoint = nullptr;
    uint32 slot = 0;
    uint32 generation = 1;
    int32 fd = INVALID_FD;
    ConnectionState state = ConnectionState::Closed;
    List<PacketStorage> send_slots;
    List<PacketStorage> receive_slots;
    BoundedQueue<uint32> pending_send_slots;
    List<byte> stream_buffer;
    size_t stream_size = 0;
    uint32 active_send_slot = INVALID_SLOT;
    size_t active_send_offset = 0;

    void Initialize(
        Endpoint* owner,
        uint32 slot_index,
        uint32 send_capacity,
        uint32 receive_capacity,
        uint32 codec_buffer_capacity)
    {
        assert(owner != nullptr);
        assert(send_capacity > 0);
        assert(receive_capacity > 0);
        assert(codec_buffer_capacity > 0);
        assert(codec_buffer_capacity <= MAX_PACKET_PAYLOAD_SIZE);

        endpoint = owner;
        slot = slot_index;
        generation = 1;
        fd = INVALID_FD;
        state = ConnectionState::Closed;
        stream_size = 0;
        active_send_slot = INVALID_SLOT;
        active_send_offset = 0;

        send_slots.clear();
        send_slots.resize(send_capacity);
        pending_send_slots.Initialize(send_capacity);

        for (PacketStorage& packet : send_slots)
        {
            packet.Initialize(codec_buffer_capacity);
        }

        receive_slots.clear();
        receive_slots.resize(receive_capacity);
        for (PacketStorage& packet : receive_slots)
        {
            packet.Initialize(codec_buffer_capacity);
        }

        stream_buffer.clear();
        stream_buffer.resize(static_cast<size_t>(PACKET_HEADER_SIZE) + codec_buffer_capacity);
    }

    ConnectionHandle Handle() const noexcept;
    EndpointId EndpointIdValue() const noexcept;

    uint32 AcquireSendSlot() noexcept
    {
        for (uint32 i = 0; i < send_slots.size(); ++i)
        {
            if (!send_slots[i].in_use)
            {
                send_slots[i].in_use = true;
                send_slots[i].size = 0;
                return i;
            }
        }
        return INVALID_SLOT;
    }

    uint32 AcquireReceiveSlot() noexcept
    {
        for (uint32 i = 0; i < receive_slots.size(); ++i)
        {
            if (!receive_slots[i].in_use)
            {
                receive_slots[i].in_use = true;
                receive_slots[i].size = 0;
                return i;
            }
        }
        return INVALID_SLOT;
    }

    void ReleaseSendSlot(uint32 packet_slot) noexcept
    {
        if (packet_slot < send_slots.size())
        {
            send_slots[packet_slot].in_use = false;
            send_slots[packet_slot].size = 0;
        }
    }

    void ReleaseReceiveSlot(uint32 packet_slot) noexcept
    {
        if (packet_slot < receive_slots.size())
        {
            receive_slots[packet_slot].in_use = false;
            receive_slots[packet_slot].size = 0;
        }
    }

    void ResetTransport() noexcept
    {
#if defined(__linux__)
        if (fd != INVALID_FD)
        {
            close(fd);
        }
#endif
        fd = INVALID_FD;
        state = ConnectionState::Closed;
        stream_size = 0;
        active_send_slot = INVALID_SLOT;
        active_send_offset = 0;
        pending_send_slots.Clear();

        for (PacketStorage& packet : send_slots)
        {
            packet.in_use = false;
            packet.size = 0;
        }

        for (PacketStorage& packet : receive_slots)
        {
            packet.in_use = false;
            packet.size = 0;
        }
    }
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
