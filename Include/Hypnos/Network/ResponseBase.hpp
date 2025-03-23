#pragma once

#include "NetworkDefinition.hpp"
#include "PacketWriter.hpp"
#include <Hypnos-Core/Container/ForwardListEx.hpp>
#include <Hypnos/Logging.hpp>
#include <forward_list>

namespace Blanketmen {
namespace Hypnos {

class ResponseBase
{
public:
    Container::ForwardListEx<ConnectionHandle> conns;

    uint8* buffer;
    packet_size length;
    
    virtual ~ResponseBase() = 0;

    void Pack(uint8* buf, int32 cap)
    {
        buffer = buf;
        offset = sizeof(packet_size);
        PackHeader();
        PackBody();

        // TODO: Compress
        // TODO: Encrypt
        length = offset;
        offset = 0;
        //*(buffer) = 1;
        byte a;
        byte b;
        byte c = a;

        PacketWriter::WriteInt16(buffer, offset, length - sizeof(packet_size)); // TODO: Sholud header be included?
    }

protected:
    int32 offset;

    MessageHeader header;

    inline void PackHeader()
    {
        PacketWriter::WriteUInt16(buffer, offset, header.msgId);
    }

    virtual void PackBody() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen