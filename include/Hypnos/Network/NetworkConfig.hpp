#pragma once

#include "IMessage.hpp"
#include "NetworkDefs.hpp"
#include <Hypnos-Core/Container/List.hpp>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/mman.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct IOUringConfig
{
    uint32 sq_entries = 8192;
    uint32 cq_entries = 8192;
    uint32 flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN | IORING_SETUP_COOP_TASKRUN;
    uint32 sq_thread_idle = 8000;

    int32 event_fd_flags = EFD_NONBLOCK | EFD_CLOEXEC;
};

struct IOBufferConfig
{
    uint16 id = 0;
    uint32 nentries = 0;
    uint32 buffer_size = MAX_BUFFER_SIZE;
    int32 mmap_flags = MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB;
};

struct ServerConfig
{
    uint8 id = 0;
    TransportProtocol protocol;
    List<uint16> shard_ids;
    in6_addr bind_ip;
    uint16 bind_port;
    int32 listen_backlog = 256;
    uint16 io_buf_gid = 0;
    int32 max_conns;
    IMessageAllocator* request_allocator = nullptr;
    IMessageAllocator* response_allocator = nullptr;
    uint32 send_buffer_size = MAX_BUFFER_SIZE;
    uint32 send_buffer_pool_capacity = 8192;
    uint32 max_pending_send_buffers_per_connection = 8;
    int32 send_buffer_mmap_flags = MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB;
};

struct ClientConfig
{
    uint8 id = 0;
    TransportProtocol protocol;
    List<uint16> shard_ids;
    in6_addr server_ip;
    uint16 server_port;
    uint16 io_buf_gid = 0;
    IMessageAllocator* response_allocator = nullptr;
};

struct NetworkConfig
{
    uint16 shard_count = 0;
    uint32 shard_cpu_index = 0;
    List<uint32> shard_cpu_ids;
    IOUringConfig io_uring;
    List<IOBufferConfig> io_buffers;
    List<ServerConfig> servers;
    List<ClientConfig> clients;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
