#pragma once

#include "RequestBase.hpp"
#include <Hypnos-Core/Type.hpp>
#include <mutex>

namespace Blanketmen {
namespace Hypnos {

class RequestProducerBase
{
public:
    virtual ~RequestProducerBase() { }

    virtual RequestBase* Produce(PacketBuffer& source, ConnectionId* connId) = 0;
};

} // namespace Hypnos
} // namespace Blanketmen