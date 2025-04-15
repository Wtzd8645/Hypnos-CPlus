#pragma once

#include "NetworkDefs.hpp"
#include "RequestAllocatorBase.hpp"
#include "ResponseAllocatorBase.hpp"
#include <netinet/in.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct SocketConfig
{
    uint8 id;
    TransportProtocol protocol;
    in6_addr ip;
    uint16 port;

    int32 max_conns;
    RequestAllocatorBase* request_allocator;
    ResponseAllocatorBase* response_allocator;
};

struct NetworkConfig
{
    Container::List<SocketConfig> server_configs;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen