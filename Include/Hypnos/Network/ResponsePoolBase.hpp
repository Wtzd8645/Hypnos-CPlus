#pragma once

#include "ResponseBase.hpp"
#include <Hypnos-Core/Types.hpp>

namespace Blanketmen {
namespace Hypnos {

class ResponsePoolBase
{
public:
    virtual ~ResponsePoolBase() = default;

    virtual ResponseBase* Acquire(uint16* id) = 0;
    virtual void Release(ResponseBase* resp) = 0;
};

} // namespace Hypnos
} // namespace Blanketmen