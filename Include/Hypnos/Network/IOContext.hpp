#pragma once

#include "Connection.hpp"
#include "IOBuffer.hpp"
#include "NetworkConfig.hpp"
#include <Hypnos-Core/Base/Memory/MemoryUtils.hpp>
#include <Hypnos-Core/Cache/SpscBufferPool.hpp>
#include <array>
#include <cstring>
#include <liburing.h>
#include <limits>
#include <sys/eventfd.h>
#include <unistd.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

enum class SocketOp : uint8
{
    Accept,
    Receive,
    Poll,
    Send
};

union BufferMetadata
{
    inline static BufferMetadata& Get(byte* buf, uint32 offset) { return *reinterpret_cast<BufferMetadata*>(buf + offset); }

    struct
    {
        ConnectionHandle conn_handle;
        int32 bid;
        packet_size offest;
        packet_size size;
    } recv;

    struct
    {
        int32 conn_count;
        packet_size size;
    } send;
};

struct IOEventArgs
{
    SocketOp op;
    uint8 ep_ver;
    uint8 ep_id;
    uint16 gid;
    uint8 conn_ver;
    Connection* conn;
};

struct IOEvent
{
    int32 res;
    uint32 flags;
    IOEventArgs* args;
};

struct ResponseArgs
{
    List<ConnectionHandle>* conn_handles;
    byte* buffer;
    packet_size length;
};

struct IOContext
{
    static constexpr size_t IO_BUFFER_GROUP_SIZE = 8;
    static constexpr size_t BUF_META_OFFSET = MemoryUtils::AlignUp(MAX_PACKET_SIZE, alignof(BufferMetadata));

    io_uring ring { };
    io_uring_params params { };
    bool ring_initialized = false;

    int32 event_fd = -1;
    IOBuffer* io_buffers[IO_BUFFER_GROUP_SIZE] = { };
    SpscBufferPool send_buf_pool;

    IOContext(const IOUringConfig& cfg) : send_buf_pool(cfg.send_buffer_size, cfg.send_pool_capacity, cfg.send_pool_mmap_flags)
    {
        event_fd = eventfd(0, cfg.event_fd_flags);
    }

    ~IOContext()
    {
        DestroyBuffers();

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

    IOContext(const IOContext&) = delete;
    IOContext& operator=(const IOContext&) = delete;

    IOContext(IOContext&&) = delete;
    IOContext& operator=(IOContext&&) = delete;

    inline Status<void> Initialize(const IOUringConfig& cfg)
    {
        memset(&params, 0, sizeof(params));
        params.flags = cfg.flags;
        params.sq_thread_idle = cfg.sq_thread_idle;

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
        return Status<void>::Success();
    }

    inline Status<void> CreateBuffer(const IOBufferConfig& cfg)
    {
        if (!ring_initialized)
        {
            return Status<void>::Error(ErrorCode::NotReady, "IOContext is not initialized.");
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
        assert(gid < IO_BUFFER_GROUP_SIZE && "[IOContext] Invalid IO buffer gid.");
        const IOBuffer* buf = io_buffers[gid];
        assert(buf != nullptr && "[IOContext] IO buffer is not initialized.");
        return (*buf)[bid];
    }

    inline void ReleaseBuffer(uint16 gid, uint32 bid) const
    {
        assert(gid < IO_BUFFER_GROUP_SIZE && "[IOContext] Invalid IO buffer gid.");
        IOBuffer* buf = io_buffers[gid];
        assert(buf != nullptr && "[IOContext] IO buffer is not initialized.");
        buf->Return(bid);
    }

    inline void FlushBuffers()
    {
        for (IOBuffer* buf : io_buffers)
        {
            buf->Flush();
        }
    }

    inline void AdvanceCqeRing(uint32 head)
    {
        io_uring_cq_advance(&ring, head - ring.cq.khead[0]);
    }

private:
    static inline bool IsPowerOfTwo(uint32 value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
