#pragma once

#include "Network.hpp"
#include "RequestFactoryBase.hpp"

namespace Blanketmen {
namespace Hypnos {

struct ConnectionListenerConfig
{
    TransportProtocol protocol;
    int32 maxConnections;
    int32 maxPacketBytes;
    RequestFactoryBase* request_factory;
};

struct NetworkConfig
{
    int32 max_conns;
    ConnectionListenerConfig listenerCfg;
};

} // namespace Hypnos
} // namespace Blanketmen