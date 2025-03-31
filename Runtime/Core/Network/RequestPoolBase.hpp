#pragma once

#include "RequestBase.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class RequestPoolBase
{
public:
    virtual ~RequestPoolBase() = default;

    virtual RequestBase* Acquire(uint8* buf) = 0;
    virtual void Release(RequestBase* resp) = 0;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen