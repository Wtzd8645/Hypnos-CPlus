#pragma once

#include "Hypnos/Network/ConnectionHandle.hpp"
#include "Hypnos/Network/IMessage.hpp"
#include "Hypnos/Network/NetworkDefs.hpp"
#include <Hypnos-Core/Container/SpscRingBuffer.hpp>
#include <algorithm>
#include <sys/socket.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class Server;
struct Connection;
struct NetworkShard;

enum class ServerShardCommandType : uint8
{
    Close,
    Send
};

struct ServerShardCommand
{
    ServerShardCommandType type = ServerShardCommandType::Send;
    Connection* conn = nullptr;
    uint32 generation = 0;
    byte* buffer = nullptr;
};

struct ServerShard
{
    NetworkShard& shard;
    Server* server = nullptr;

    int32 listen_fd = INVALID_FD;
    sockaddr_storage accept_addr { };
    socklen_t accept_addr_len = sizeof(sockaddr_storage);

    SpscRingBuffer<ConnectionEvent> egress_connection_events;
    SpscRingBuffer<IMessage*> egress_requests;
    SpscRingBuffer<ServerShardCommand> ingress_commands;

    ServerShard(Server* server, NetworkShard& shard, size_t queue_capacity_hint, uint32 max_pending_buffers) :
        shard(shard),
        server(server),
        egress_connection_events(std::max<size_t>(MIN_SHARD_QUEUE_CAPACITY, queue_capacity_hint)),
        egress_requests(std::max<size_t>(MIN_SHARD_QUEUE_CAPACITY, queue_capacity_hint)),
        ingress_commands(ResolveIngressQueueCapacity(queue_capacity_hint, max_pending_buffers))
    {
    }

private:
    static constexpr size_t MIN_SHARD_QUEUE_CAPACITY = 64;
    static constexpr size_t EVENT_ARGS_SLACK = 8;

    static size_t ResolveIngressQueueCapacity(size_t connection_capacity, uint32 max_pending_buffers) noexcept
    {
        return std::max<size_t>(MIN_SHARD_QUEUE_CAPACITY, connection_capacity * std::max<uint32>(1, max_pending_buffers) + EVENT_ARGS_SLACK);
    }
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
