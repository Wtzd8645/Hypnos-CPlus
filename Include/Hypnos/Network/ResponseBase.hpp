#pragma once

#include "NetworkDefinition.hpp"
#include "PacketWriter.hpp"
#include <Hypnos-Core/Container/Vector.hpp>
#include <Hypnos/Logging.hpp>

namespace Blanketmen {
namespace Hypnos {

class ResponseBase
{
public:
    virtual ~ResponseBase() = 0;

    Container::Vector<Connection*> conns;
    char_ptr buffer;
    int32 offset;

    PacketLengthSize Pack(PacketBuffer& src)
    {
        buffer = src.data + src.offset;
        offset = sizeof(PacketLengthSize);

        PackHeader();
        PackBody();

        // TODO: Compress
        // TODO: Encrypt
        PacketLengthSize len = offset;
        offset = 0;
        PacketWriter::WriteInt16(buffer, offset, len - sizeof(PacketLengthSize));
        return len;
    }

protected:
    MessageHeader header;

    inline void PackHeader()
    {
        PacketWriter::WriteUInt16(buffer, offset, header.msgId);
    }

    virtual void PackBody() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen