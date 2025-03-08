#pragma once

#include "RequestBase.hpp"
#include <Hypnos-Core/Types.hpp>
#include <mutex>

namespace Blanketmen {
namespace Hypnos {

class RequestFactoryBase
{
public:
    virtual ~RequestFactoryBase() = default;

    virtual RequestBase* Create(uint8* buf, Connection* conn) = 0;
    virtual void Recycle(RequestBase* req) = 0;
};

} // namespace Hypnos
} // namespace Blanketmen