#pragma once

#include "Hypnos/Network/ConnectionHandle.hpp"
#include "Hypnos/Network/NetworkDefs.hpp"
#include <limits>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct RecvContext
{
    PacketSize received_bytes = 0;
    TransportHeader header = { };
    byte* buffer = nullptr;
};

enum class ConnectionState : uint8
{
    Vacant,
    Connected,
    Closing
};

struct PendingBufferRing
{
    void Bind(byte** storage, uint32 cap) noexcept
    {
        buffers = storage;
        capacity = cap;
        Reset();
    }

    void Reset() noexcept
    {
        head = 0;
        tail = 0;
    }

    bool IsEmpty() const noexcept
    {
        return head == tail;
    }

    bool IsFull() const noexcept
    {
        return capacity != 0 && (tail - head) == capacity;
    }

    bool Push(byte* buffer) noexcept
    {
        if (buffer == nullptr || capacity == 0 || IsFull())
        {
            return false;
        }

        buffers[tail % capacity] = buffer;
        ++tail;
        return true;
    }

    bool Pop(byte*& buffer) noexcept
    {
        if (IsEmpty())
        {
            return false;
        }

        buffer = buffers[head % capacity];
        ++head;
        return true;
    }

    byte** buffers = nullptr;
    uint32 capacity = 0;
    uint32 head = 0;
    uint32 tail = 0;
};

struct SendContext
{
    PacketSize pending_bytes = 0;
    PacketSize processed_bytes = 0;
    byte* active_buffer = nullptr;
    PendingBufferRing pending_buffers;
};

struct Connection
{
    ConnectionHandle CreateHandle() const noexcept
    {
        return ConnectionHandle(endpoint_id,
                                shard_id,
                                slot,
                                generation.load(std::memory_order_acquire));
    }

    uint16 endpoint_id = std::numeric_limits<uint16>::max();
    uint16 shard_id = 0;
    uint16 slot = 0;

    Atomic<ConnectionState> state { ConnectionState::Vacant };
    Atomic<uint32> generation { 1 };
    Atomic<uint32> queued_send_count { 0 };

    int32 sock_fd = INVALID_FD;
    RecvContext recv_ctx;
    SendContext send_ctx;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
