#pragma once

#include "NetworkDefs.hpp"
#include "RequestFactoryBase.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

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

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen