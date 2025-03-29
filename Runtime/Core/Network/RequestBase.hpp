#pragma once

#include "Network.hpp"
#include "PacketReader.hpp"
#include <Hypnos-Kernel.hpp>

namespace Blanketmen {
namespace Hypnos {

class RequestBase
{
public:
    ConnectionHandle conn_handle;
    uint16 gid;
    uint16 id;

    virtual ~RequestBase() { }

    void Unpack(uint8* buf)
    {
        // TODO: Decrypt
        // TODO: Decompress
        buffer = buf;
        offset = 0;
        UnpackBody();
    }

protected:
    uint8* buffer;
    int32 offset;

    virtual void UnpackBody() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen