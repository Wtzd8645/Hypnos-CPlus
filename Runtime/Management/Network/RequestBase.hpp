#ifndef REQUEST_BASE_HPP_
#define REQUEST_BASE_HPP_

#include <Foundation/Logging.hpp>
#include "NetworkDefinition.hpp"
#include "PacketReader.hpp"

namespace Blanketmen {
namespace Hypnos {

class RequestBase
{
public:
    virtual ~RequestBase() { }

    ConnectionId* connId;
    MessageHeader header;

    void Unpack(PacketBuffer& src)
    {
        // TODO: Decrypt
        // TODO: Decompress
        state.buffer = src.final + src.offset;
        state.offset = 0;
        UnpackBody();
    }

protected:
    SerializationState state;

    virtual void UnpackBody() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // REQUEST_BASE_HPP_