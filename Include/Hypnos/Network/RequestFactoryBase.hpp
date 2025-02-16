#pragma once

#include "RequestBase.hpp"
#include <Hypnos-Core/Type.hpp>
#include <mutex>

namespace Blanketmen {
namespace Hypnos {

class RequestFactoryBase
{
public:
    virtual ~RequestFactoryBase() = default;

    virtual RequestBase* Create(char_ptr buf, Connection* conn) = 0;
    virtual void Recycle(RequestBase* req) = 0;
};

} // namespace Hypnos
} // namespace Blanketmen