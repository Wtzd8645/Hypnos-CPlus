#pragma once

#include "Connection.hpp"
#include "NetworkDefs.hpp"
#include "PacketWriter.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct ResponseArgs
{
    Container::List<ConnectionHandle>* conn_handles;
    uint8* buffer;
    packet_size length;
};

class ResponseBase
{
public:
    virtual ~ResponseBase() = 0;

    // TODO: Check message size.
    packet_size Pack(uint8* buf)
    {
        buffer = buf;
        offset = sizeof(packet_size);
        PackInternal();

        packet_size len = offset - sizeof(packet_size);
        offset = 0;
        PacketWriter::WriteInt16(buffer, offset, len);
        return len;
    }

protected:
    uint8* buffer;
    int32 offset;

    virtual void PackInternal() = 0;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen