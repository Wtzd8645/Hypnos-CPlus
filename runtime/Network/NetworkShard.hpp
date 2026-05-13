#pragma once

#include "Connection.hpp"
#include "Hypnos/Network/IOBuffer.hpp"
#include "Hypnos/Network/NetworkConfig.hpp"
#include <Hypnos-Core/Base/Memory/MemoryUtils.hpp>
#include <Hypnos-Core/Cache/BufferPool.hpp>
#include <Hypnos-Core/Cache/ObjectPool.hpp>
#include <Hypnos-Core/Container/List.hpp>
#include <Hypnos-Core/Container/SpscRingBuffer.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <liburing.h>
#include <limits>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <unistd.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct CompletionArgs;
using CompletionHandler = void (*)(int32 res, uint32 flags, CompletionArgs* args);

struct CompletionArgs
{
    CompletionHandler complete = nullptr;
    void* user_data = nullptr;
    uint32 generation = 0;
};

struct BufferMetadata
{
    struct Send
    {
        Atomic<uint32> ref_count;
        PacketSize size;
        PacketSize reserved;
    };

    static constexpr size_t SEND_OFFSET = MemoryUtils::AlignUp(MAX_PACKET_SIZE, alignof(Send));

    inline static Send& GetSend(byte* buf) { return reinterpret_cast<BufferMetadata*>(buf + SEND_OFFSET)->send; }

    Send send;
};

struct NetworkShard
{
    using NetworkBufferPool = BufferPool<MmapAllocatePolicy<MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB>>;

    enum class State : uint8
    {
        Stopped,
        Running,
        Stopping
    };

    static constexpr size_t IO_BUFFER_GROUP_SIZE = 8;
    static constexpr size_t MIN_RETURNED_SEND_BUFFER_QUEUE_CAPACITY = 64;
    alignas(CACHE_LINE_SIZE) Atomic<uint32> pending_commands { 0 };

    Atomic<State> state { State::Stopped };
    uint16 id = 0;
    uint32 cpu_id = 0;
    int32 event_fd = -1;
    void* owner = nullptr;
    Thread* thread = nullptr;

    io_uring ring { };
    io_uring_params params { };
    IOBuffer* io_buffers[IO_BUFFER_GROUP_SIZE] = { };

    CompletionArgs wake_poll_args { };
    HeapObjectPool<CompletionArgs> args_pool;
    Connection* connections = nullptr;
    size_t connection_capacity = 0;
    uint32 pending_send_capacity = 0;
    List<uint32> free_connection_slots;
    List<byte*> pending_buffer_storage;
    NetworkBufferPool framing_buffer_pool;
    NetworkBufferPool send_buffer_pool;
    SpscRingBuffer<byte*> returned_send_buffers;

    bool ring_initialized = false;
    bool needs_buffer_flush = false;
    size_t args_pool_capacity = 0;
    size_t framing_buffer_pool_capacity;
    size_t send_buffer_pool_capacity;

    NetworkShard(const IOUringConfig& cfg,
                 size_t args_pool_capacity,
                 size_t framing_buffer_pool_capacity,
                 size_t send_buffer_pool_capacity,
                 size_t connection_capacity,
                 uint32 pending_send_capacity) :
        connection_capacity(connection_capacity),
        pending_send_capacity(std::max<uint32>(1, pending_send_capacity)),
        framing_buffer_pool(MAX_BUFFER_SIZE),
        send_buffer_pool(MAX_BUFFER_SIZE),
        returned_send_buffers(std::max<size_t>(MIN_RETURNED_SEND_BUFFER_QUEUE_CAPACITY, std::max<size_t>(1, send_buffer_pool_capacity))),
        args_pool_capacity(args_pool_capacity),
        framing_buffer_pool_capacity(framing_buffer_pool_capacity),
        send_buffer_pool_capacity(send_buffer_pool_capacity)
    {
        event_fd = eventfd(0, cfg.event_fd_flags);
        if (connection_capacity > 0)
        {
            connections = new Connection[connection_capacity];
            free_connection_slots.reserve(connection_capacity);
            pending_buffer_storage.assign(connection_capacity * this->pending_send_capacity, nullptr);
        }
    }

    ~NetworkShard()
    {
        delete[] connections;
        DestroyBuffers();

        if (thread != nullptr)
        {
            delete thread;
            thread = nullptr;
        }

        if (ring_initialized)
        {
            io_uring_queue_exit(&ring);
            ring_initialized = false;
        }

        if (event_fd >= 0)
        {
            close(event_fd);
        }
    }

    NetworkShard(const NetworkShard&) = delete;
    NetworkShard& operator=(const NetworkShard&) = delete;

    NetworkShard(NetworkShard&&) = delete;
    NetworkShard& operator=(NetworkShard&&) = delete;

    inline Status<void> Initialize(const IOUringConfig& cfg, uint16 shard_id)
    {
        memset(&params, 0, sizeof(params));
        params.flags = cfg.flags;
        params.sq_thread_idle = cfg.sq_thread_idle;
        this->id = shard_id;
        wake_poll_args.user_data = this;

        if (cfg.cq_entries > cfg.sq_entries)
        {
            params.flags |= IORING_SETUP_CQSIZE;
            params.cq_entries = cfg.cq_entries;
        }

        int32 err = io_uring_queue_init_params(cfg.sq_entries, &ring, &params);
        if (err < 0)
        {
            return Status<void>::Error(ErrorCode::InitializationFailed, "Failed to initialize io_uring.");
        }

        ring_initialized = true;
        InitializeConnectionPool();
        return Status<void>::Success();
    }

    inline Status<void> AllocateSharedResources()
    {
        if (args_pool_capacity != 0 && args_pool.Capacity() == 0)
        {
            Status<void> status = args_pool.Allocate(args_pool_capacity);
            if (status.IsFailed())
            {
                return status;
            }
        }

        if (framing_buffer_pool_capacity != 0 && framing_buffer_pool.Capacity() == 0)
        {
            Status<void> status = framing_buffer_pool.Allocate(framing_buffer_pool_capacity);
            if (status.IsFailed())
            {
                return status;
            }
        }

        if (send_buffer_pool_capacity != 0 && send_buffer_pool.Capacity() == 0)
        {
            Status<void> status = send_buffer_pool.Allocate(send_buffer_pool_capacity);
            if (status.IsFailed())
            {
                return status;
            }
        }

        return Status<void>::Success();
    }

    inline io_uring_sqe* AcquireSqe()
    {
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        if (sqe != nullptr)
        {
            return sqe;
        }

        if (io_uring_submit(&ring) < 0)
        {
            return nullptr;
        }

        return io_uring_get_sqe(&ring);
    }

    inline io_uring_sqe* PrepareSqe(CompletionArgs* args)
    {
        assert(args != nullptr && "[NetworkShard] Completion args must not be null.");

        io_uring_sqe* sqe = AcquireSqe();
        if (sqe == nullptr)
        {
            return nullptr;
        }

        io_uring_sqe_set_data(sqe, args);
        return sqe;
    }

    inline Status<void> SubmitWakePoll()
    {
        io_uring_sqe* sqe = PrepareSqe(&wake_poll_args);
        if (sqe == nullptr)
        {
            return Status<void>::Error(ErrorCode::Busy, "Failed to acquire SQE for shard wake poll.");
        }

        io_uring_prep_poll_multishot(sqe, event_fd, POLLIN);
        return Status<void>::Success();
    }

    inline Status<void> CreateBuffer(const IOBufferConfig& cfg)
    {
        if (!ring_initialized)
        {
            return Status<void>::Error(ErrorCode::NotReady, "NetworkShard is not initialized.");
        }

        if (cfg.buffer_size == 0 || !IsPowerOfTwo(cfg.nentries))
        {
            return Status<void>::Error(ErrorCode::InvalidArgument, "Recv group entries must be power-of-two and buffer size must be greater than zero.");
        }

        if (io_buffers[cfg.id] != nullptr)
        {
            return Status<void>::Error(ErrorCode::AlreadyExists, "IOBuffer already exists.");
        }

        IOBuffer* buf = new IOBuffer(cfg);
        Status<void> status = buf->Initialize(&ring);
        if (status.IsFailed())
        {
            delete buf;
            return status;
        }

        io_buffers[cfg.id] = buf;
        return Status<void>::Success();
    }

    inline void DestroyBuffers()
    {
        for (uint32 i = 0; i < IO_BUFFER_GROUP_SIZE; ++i)
        {
            IOBuffer* buf = io_buffers[i];
            if (buf != nullptr)
            {
                buf->Destroy(&ring, ring_initialized);
                delete buf;
                io_buffers[i] = nullptr;
            }
        }
    }

    inline byte* AcquireBuffer(uint16 gid, uint32 bid) const
    {
        assert(gid < IO_BUFFER_GROUP_SIZE && "[NetworkShard] Invalid IO buffer gid.");
        const IOBuffer* buf = io_buffers[gid];
        assert(buf != nullptr && "[NetworkShard] IO buffer is not initialized.");
        return (*buf)[bid];
    }

    inline void ReleaseBuffer(uint16 gid, uint32 bid)
    {
        assert(gid < IO_BUFFER_GROUP_SIZE && "[NetworkShard] Invalid IO buffer gid.");
        IOBuffer* buf = io_buffers[gid];
        assert(buf != nullptr && "[NetworkShard] IO buffer is not initialized.");
        buf->Return(bid);
        needs_buffer_flush = true;
    }

    inline void FlushBuffers()
    {
        if (!needs_buffer_flush)
        {
            return;
        }

        for (IOBuffer* buf : io_buffers)
        {
            if (buf != nullptr && buf->HasPending())
            {
                buf->Flush();
            }
        }

        needs_buffer_flush = false;
    }

    inline void AdvanceCqeRing(uint32 count)
    {
        io_uring_cq_advance(&ring, count);
    }

    inline void DrainReturnedSendBuffers()
    {
        if (send_buffer_pool.Capacity() == 0)
        {
            return;
        }

        byte* buffer = nullptr;
        while (returned_send_buffers.Dequeue(buffer))
        {
            send_buffer_pool.Release(buffer);
        }
    }

    inline void Notify() noexcept
    {
        if (pending_commands.fetch_add(1, std::memory_order_release) == 0)
        {
            eventfd_write(event_fd, 1);
        }
    }

    inline void ClearWakeEvent() const noexcept
    {
        eventfd_t count = 0;
        while (eventfd_read(event_fd, &count) == 0) { }
    }

    inline uint32 ConsumePendingCommands() noexcept
    {
        return pending_commands.exchange(0, std::memory_order_acq_rel);
    }

    inline Connection* AcquireConnection(void* endpoint_context, uint32 requested_pending_send_capacity)
    {
        assert(requested_pending_send_capacity <= pending_send_capacity && "[NetworkShard] Requested pending send capacity exceeds shard storage stride.");
        if (free_connection_slots.empty() || requested_pending_send_capacity > pending_send_capacity)
        {
            return nullptr;
        }

        const uint32 slot_index = free_connection_slots.back();
        free_connection_slots.pop_back();

        Connection& conn = connections[slot_index];
        assert(conn.state.load(std::memory_order_acquire) == Connection::State::Vacant && "[NetworkShard] Free connection slot must be vacant.");
        conn.endpoint_context = endpoint_context;
        conn.slot = static_cast<uint16>(slot_index);
        conn.queued_send_count.store(0, std::memory_order_release);
        conn.send_ctx.pending_buffers.Reset();
        return &conn;
    }

    inline bool ReleaseConnection(Connection& conn)
    {
        assert(connections != nullptr && "[NetworkShard] Connection pool is not initialized.");
        assert(conn.slot < connection_capacity && "[NetworkShard] Connection slot is out of bounds.");

        Connection::State expected = Connection::State::Closing;
        if (!conn.state.compare_exchange_strong(expected, Connection::State::Vacant, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return false;
        }

        conn.sock_fd = INVALID_FD;
        conn.endpoint_context = nullptr;
        conn.queued_send_count.store(0, std::memory_order_release);
        conn.send_ctx.pending_buffers.Reset();
        free_connection_slots.push_back(conn.slot);
        return true;
    }

private:
    inline void InitializeConnectionPool()
    {
        free_connection_slots.clear();
        for (size_t slot_index = 0; slot_index < connection_capacity; ++slot_index)
        {
            Connection& conn = connections[slot_index];
            conn.endpoint_context = nullptr;
            conn.slot = static_cast<uint16>(slot_index);
            conn.send_ctx.pending_buffers.Bind(pending_buffer_storage.data() + slot_index * pending_send_capacity, pending_send_capacity);
            free_connection_slots.push_back(static_cast<uint32>(connection_capacity - 1 - slot_index));
        }
    }

    static inline bool IsPowerOfTwo(uint32 value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
