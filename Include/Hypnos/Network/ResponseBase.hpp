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
    virtual ~ResponseBase() { }

    Container::Vector<ConnectionId*> connIds;
    
    PacketLengthSize Pack(PacketBuffer& src)
    {
        state.buffer = src.final + src.offset;
        state.offset = sizeof(PacketLengthSize);

        PackHeader();
        PackBody();

        // TODO: Compress
        // TODO: Encrypt
        PacketLengthSize length = state.offset;
        state.offset = 0;
        PacketWriter::WriteInt16(state, length - sizeof(PacketLengthSize));
        return length;
    }

protected:
    SerializationState state;
    MessageHeader header;

    inline void PackHeader()
    {
        PacketWriter::WriteUInt16(state, header.msgId);
    }

    virtual void PackBody() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen