#pragma once

#include "Connection.hpp"
#include "NetworkDefs.hpp"
#include "PacketReader.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

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

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen