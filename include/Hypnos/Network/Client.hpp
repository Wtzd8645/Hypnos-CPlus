#pragma once

#include "IMessage.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class Client
{
public:
    explicit Client(uint8 id) : id(id), response_allocator(nullptr)
    {
    }

    virtual ~Client() = default;

    const uint8 id;
    virtual Status<void> Connect() = 0;
    virtual Status<void> Disconnect() = 0;
    virtual void Send(IMessage* resp) = 0;

protected:
    IMessageAllocator* response_allocator;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
