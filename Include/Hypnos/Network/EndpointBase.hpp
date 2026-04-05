#pragma once

#include "IOContext.hpp"
#include "NetworkDefs.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class EndpointBase
{
public:
    enum class Type : uint8
    {
        Server,
        Client
    };

    explicit EndpointBase(uint8 id, Type type) : id(id), type(type) { }
    virtual ~EndpointBase() = default;

    const uint8 id;
    const Type type;

    virtual Status<void> Start() = 0;
    virtual Status<void> Stop() = 0;
    virtual void Dispatch() = 0;
    virtual void OnReactorWake(uint16 rid) { (void)rid; }

protected:
    int32 sock_fd = INVALID_FD;
    uint8 version = 0;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
