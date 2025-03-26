#pragma once

#include "RequestBase.hpp"
#include <Hypnos-Core/Types.hpp>

namespace Blanketmen {
namespace Hypnos {

class RequestPoolBase
{
public:
    virtual ~RequestPoolBase() = default;

    virtual RequestBase* Acquire(uint8* buf) = 0;
    virtual void Release(RequestBase* resp) = 0;
};

} // namespace Hypnos
} // namespace Blanketmen