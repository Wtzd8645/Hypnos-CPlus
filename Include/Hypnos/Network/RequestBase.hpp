#pragma once

#include "NetworkDefinition.hpp"
#include "PacketReader.hpp"
#include <Hypnos/Logging.hpp>

namespace Blanketmen {
namespace Hypnos {

class RequestBase
{
public:
    virtual ~RequestBase() { }

    Connection* conn;
    MessageHeader header;

    void Unpack(PacketBuffer& src)
    {
        // TODO: Decrypt
        // TODO: Decompress
        buffer.data = src.data + src.offset;
        buffer.offset = 0;
        UnpackBody();
    }

protected:
    PacketBuffer buffer;

    virtual void UnpackBody() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen