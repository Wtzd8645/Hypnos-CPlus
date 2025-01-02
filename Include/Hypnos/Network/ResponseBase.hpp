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
    PacketBuffer buffer;

    PacketLengthSize Pack(PacketBuffer& src)
    {
        buffer.data = src.data + src.offset;
        buffer.offset = sizeof(PacketLengthSize);

        PackHeader();
        PackBody();

        // TODO: Compress
        // TODO: Encrypt
        PacketLengthSize len = buffer.offset;
        buffer.offset = 0;
        PacketWriter::WriteInt16(buffer, len - sizeof(PacketLengthSize));
        return len;
    }

protected:
    MessageHeader header;

    inline void PackHeader()
    {
        PacketWriter::WriteUInt16(buffer, header.msgId);
    }

    virtual void PackBody() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen