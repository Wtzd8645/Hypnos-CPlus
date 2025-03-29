#pragma once

#include "Network.hpp"
#include "PacketWriter.hpp"

namespace Blanketmen {
namespace Hypnos {

class ResponseBase
{
public:
    virtual ~ResponseBase() = 0;

    // TODO: Check message size.
    void Pack(SocketOperationArgs& args)
    {
        buffer = args.buffer;
        offset = sizeof(packet_size);
        PackInternal();

        args.length = offset - sizeof(packet_size);
        offset = 0;
        PacketWriter::WriteInt16(buffer, offset, args.length);
    }

protected:
    uint8* buffer;
    int32 offset;

    virtual void PackInternal() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen