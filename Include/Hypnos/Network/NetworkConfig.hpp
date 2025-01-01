#pragma once

#include "NetworkDefinition.hpp"
#include "RequestProducerBase.hpp"

namespace Blanketmen {
namespace Hypnos {

struct ConnectionListenerConfig
{
    TransportProtocol protocol;
    int32 maxConnections;
    int32 maxPacketBytes;
    RequestProducerBase* requestProducer;
};

struct NetworkConfig
{
    ConnectionListenerConfig listenerCfg;
};

} // namespace Hypnos
} // namespace Blanketmen