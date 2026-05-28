#pragma once

#include "IMessage.hpp"
#include "NetworkDefs.hpp"
#include <Hypnos-Core/Container/List.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct EndpointAddress
{
    string host;
    uint16 port = 0;
};

struct ServerConfig
{
    EndpointId id = INVALID_ENDPOINT_ID;
    TransportProtocol transport = TransportProtocol::Tcp;
    EndpointAddress bind;
    uint16 worker_id = 0;
    uint32 max_connections = 0;
    uint32 send_queue_capacity_per_connection = 0;
    uint32 receive_queue_capacity_per_connection = 0;
    uint32 delivery_queue_capacity = 0;
    uint32 codec_buffer_capacity = 0;
};

struct ClientConfig
{
    EndpointId id = INVALID_ENDPOINT_ID;
    TransportProtocol transport = TransportProtocol::Tcp;
    EndpointAddress remote;
    uint16 worker_id = 0;
    uint32 send_queue_capacity = 0;
    uint32 receive_queue_capacity = 0;
    uint32 delivery_queue_capacity = 0;
    uint32 codec_buffer_capacity = 0;
};

struct NetworkConfig
{
    BackendType backend = BackendType::None;
    uint16 worker_count = 0;
    List<UniquePtr<ICodec>> codecs;
    UniquePtr<IMessageAllocator> message_allocator;
    UniquePtr<IPacketPipeline> packet_pipeline;
    List<ServerConfig> servers;
    List<ClientConfig> clients;

    NetworkConfig() = default;
    ~NetworkConfig() = default;

    NetworkConfig(const NetworkConfig&) = delete;
    NetworkConfig& operator=(const NetworkConfig&) = delete;

    NetworkConfig(NetworkConfig&&) noexcept = default;
    NetworkConfig& operator=(NetworkConfig&&) noexcept = default;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
