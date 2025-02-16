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

    void Unpack(char_ptr buf)
    {
        // TODO: Decrypt
        // TODO: Decompress
        buffer = buf;
        offset = 0;
        UnpackBody();
    }

protected:
    char_ptr buffer;
    int32 offset;

    virtual void UnpackBody() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen