#pragma once

#include "Connection.hpp"
#include "IOUringBufferPool.hpp"
#include <Hypnos-Kernel/Core/Cache.hpp>
#include <cstring>
#include <liburing.h>
#include <sys/eventfd.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

union BufferMetadata
{
    inline static BufferMetadata& Get(uint8* buf, uint32 offset)
    {
        return *reinterpret_cast<BufferMetadata*>(buf + offset);
    }

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

enum class SocketOp : uint8
{
    Poll,
    Accept,
    Receive,
    Send
};

struct IOEventArgs
{
    SocketOp op;
    uint8 sock_ver;
    uint8 sock_id;
    uint8 conn_ver;
    Connection* conn;
};

class IOUringContext
{
public:
    int32 efd;
    io_uring ring;
    io_uring_params ring_params;

    io_uring_buf_ring* recv_buf_ring;
    int32 recv_buf_mask;
    int32 recv_buf_count;

    IOUringBufferPool recv_buf_pool;
    Cache::SPSC::MmapBufferPool send_buf_pool;

    IOUringContext(uint32 max_conns) :
        efd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
        recv_buf_pool(MAX_BUFFER_SIZE, MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB, max_conns),
        send_buf_pool(MAX_BUFFER_SIZE, MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB, max_conns),
        max_conns(max_conns)
    {
        std::memset(&ring_params, 0, sizeof(ring_params));
        ring_params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN; // TODO: Make configurable.
        ring_params.sq_thread_idle = 8000;
    }

    ~IOUringContext()
    {
        if (efd >= 0)
        {
            close(efd);
        }

        if (recv_buf_ring != nullptr)
        {
            io_uring_queue_exit(&ring);
            io_uring_free_buf_ring(&ring, recv_buf_ring, max_conns, 0);
            recv_buf_ring = nullptr;
        }
    }

    int32 Setup()
    {
        int32 err = io_uring_queue_init_params(max_conns, &ring, &ring_params);
        if (err < 0)
        {
            Logging::Error("[TcpSocket] Failed to initialize io_uring.");
            return err;
        }

        recv_buf_ring = io_uring_setup_buf_ring(&ring, max_conns, IO_RECV_BUF_GROUP, 0, &err);
        if (recv_buf_ring == nullptr)
        {
            Logging::Error("[TcpSocket] Failed to setup buf ring.");
            return err;
        }

        recv_buf_mask = io_uring_buf_ring_mask(max_conns);
        for (size_t i = 0; i < max_conns; ++i)
        {
            io_uring_buf_ring_add(recv_buf_ring, recv_buf_pool[i], MAX_BUFFER_SIZE, i, recv_buf_mask, i);
        }
        io_uring_buf_ring_advance(recv_buf_ring, max_conns);
        return 0;
    }

    inline void AddBufferToRing(int32 bid)
    {
        io_uring_buf_ring_add(recv_buf_ring, recv_buf_pool[bid], MAX_BUFFER_SIZE, bid, recv_buf_mask, recv_buf_count++);
    }

    inline void AdvanceBufRing()
    {
        if (recv_buf_count > 0)
        {
            io_uring_buf_ring_advance(recv_buf_ring, recv_buf_count);
            recv_buf_count = 0;
        }
    }

    inline void AdvanceCqRing(uint32 head)
    {
        io_uring_cq_advance(&ring, head - ring.cq.khead[0]);
    }

private:
    size_t max_conns;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen