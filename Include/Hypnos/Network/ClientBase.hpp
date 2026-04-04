#pragma once

#include "EndpointBase.hpp"
#include "IMessage.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class ClientBase : public EndpointBase
{
public:
    ClientBase(uint8 id) :
        EndpointBase(id, EndpointBase::Type::Client),
        response_allocator(nullptr)
    {
    }

    virtual ~ClientBase() = default;

    Status<void> Start() override
    {
        return Connect();
    }

    Status<void> Stop() override
    {
        return Disconnect();
    }

    virtual Status<void> Connect() = 0;
    virtual Status<void> Disconnect() = 0;

    virtual void Send(IMessage* resp) = 0;

protected:
    IMessageAllocator* response_allocator;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
