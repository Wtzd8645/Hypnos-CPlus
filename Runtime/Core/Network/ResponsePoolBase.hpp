#pragma once

#include "ResponseBase.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class ResponsePoolBase
{
public:
    virtual ~ResponsePoolBase() = default;

    virtual ResponseBase* Acquire(uint16 gid, uint16 id) = 0;
    virtual void Release(ResponseBase* resp) = 0;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen