#ifndef REQUEST_Producer_BASE_HPP_
#define REQUEST_Producer_BASE_HPP_

#include <mutex>
#include <Type.hpp>
#include "RequestBase.hpp"

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

#endif // REQUEST_Producer_BASE_HPP_