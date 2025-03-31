#pragma once

#include "RequestBase.hpp"
#include <mutex>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class RequestFactoryBase
{
public:
    virtual ~RequestFactoryBase() = default;

    virtual RequestBase* Create(uint8* buf, Connection* conn) = 0;
    virtual void Recycle(RequestBase* req) = 0;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen