#pragma once

#include "NetworkDefs.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

// Base class for response messages
class Response
{
public:
    virtual ~Response() = default;

    virtual packet_size Pack(uint8* buf) = 0;
    virtual void Unpack(const uint8* buf, int32 len) = 0;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
