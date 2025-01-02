#pragma once

#include "NetworkDefinition.hpp"
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
    ConnectionListenerConfig listenerCfg;
};

} // namespace Hypnos
} // namespace Blanketmen