#pragma once

#include "IoUringBufferPool.hpp"
#include <Hypnos-Kernel.hpp>
#include <liburing.h>
#include <sys/eventfd.h>

namespace Blanketmen {
namespace Hypnos {

constexpr int32 MAX_ETH_MTU = 1500; // Ethernet (Standard) MTU.
constexpr int32 MAX_WIFI_MTU = 1500; // Wi-Fi (802.11) MTU.
constexpr int32 MAX_PPPoE_MTU = 1492; // PPPoE (DSL) MTU.
constexpr int32 MAX_VPN_MTU = 1476; // VPN (GRE Tunnel) MTU.
constexpr int32 MAX_JUMBO_MTU = 9000; // Jumbo Frames MTU.
constexpr int32 MAX_LOOPBACK_MTU = 65536; // Loopback (lo Interface) MTU.
constexpr int32 MIN_IPV6_MTU = 1280; // IPv6 (Minimum) MTU.

constexpr int32 MAX_PACKET_SIZE = MAX_VPN_MTU;
constexpr int32 MAX_BUFFER_SIZE = 2048;
constexpr int32 IO_RECV_BUF_GROUP = 0;

constexpr const int32 INVALID_FD = -1;
constexpr const int32 SOCKET_ERROR = -1;
constexpr const uint16 DEFAULT_PORT = 27015;

typedef int16 packet_size;

enum class EndpointRole : uint8
{
    Server,
    Client
};

enum class ConnectionEventId : uint8
{
    CONNECT,
    DISCONNECT
};

enum TransportProtocol : uint8
{
    LOCAL_SIMULATION = 0,
    TCP = 1,
    UDP = 2,
    RUDP = 3
};

enum socket_op : uint8
{
    ACPT,
    RECV,
    POLL,
    SEND
};

struct recv_context
{
    uint8 buffer[MAX_BUFFER_SIZE];
    packet_size packet_bytes = 0;
    packet_size waiting_bytes = sizeof(packet_size);
    packet_size received_bytes = 0;
};

struct send_context
{
    uint8* buffer = nullptr;
    packet_size pending_bytes = 0;
    packet_size processed_bytes = 0;
    Container::Queue<uint8*> pending_responses;
};

struct Connection
{
    int32 index;
    int32 sock_fd = INVALID_FD;
    uint8 version = 0;
    recv_context recv_ctx;
    send_context send_ctx;
};

struct ConnectionHandle
{
    Connection* conn;
    uint8 version;

    inline operator Connection* () { return conn; }
};

union buffer_metadata
{
    inline static buffer_metadata& get(uint8* buf, uint32 offset)
    {
        return *reinterpret_cast<buffer_metadata*>(buf + offset);
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

struct io_event_args
{
    Connection* conn;
    uint8 conn_ver;
    socket_op op;
    int8 sock_id;
    int8 sock_ver;
};

struct io_uring_context
{
    int32 efd;
    io_uring ring;
    io_uring_params ring_params;

    io_uring_buf_ring* recv_buf_ring;
    int32 recv_buf_mask;
    int32 recv_buf_count;

    Cache::IoUringBufferPool recv_buf_pool;
    Cache::SPSC::MmapBufferPool send_buf_pool;
    Cache::ObjectPool<io_event_args> event_args_pool;

    io_uring_context(uint max_conns) :
        recv_buf_pool(MAX_BUFFER_SIZE, MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB, max_conns),
        send_buf_pool(MAX_BUFFER_SIZE, MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB, max_conns),
        event_args_pool(max_conns)
    {
        efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        std::memset(&ring_params, 0, sizeof(ring_params));
        ring_params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN; // TODO: Make configurable.
        ring_params.sq_thread_idle = 8000;
    }

    inline void advance_buf_ring()
    {
        if (recv_buf_count > 0)
        {
            io_uring_buf_ring_advance(recv_buf_ring, recv_buf_count);
            recv_buf_count = 0;
        }
    }
};

struct SocketOperationArgs
{
    Container::List<ConnectionHandle>* conn_handles;
    uint8* buffer;
    packet_size length;
};

struct ConnectionEvent
{
    ConnectionEventId evtId;
    Connection* conn;
};

struct MessageHeader
{
    uint8 seq;
    uint16 msgId;
};

} // namespace Hypnos
} // namespace Blanketmen