#pragma once

#include "NetworkDefs.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct IMessage
{
    virtual ~IMessage() = default;

    virtual uint8 CodecId() const noexcept = 0;
};

class IMessageAllocator
{
public:
    virtual ~IMessageAllocator() = default;

    virtual IMessage* Acquire(uint8 codec_id) = 0;
    virtual void Release(IMessage& message) = 0;
};

class ICodec
{
public:
    virtual ~ICodec() = default;

    virtual uint8 Id() const noexcept = 0;
    virtual Status<PacketSize> Encode(IMessage& message, byte* buffer, PacketSize capacity) = 0;
    virtual Status<IMessage*> Decode(const byte* buffer, PacketSize size, IMessageAllocator& allocator) = 0;
};

class IPacketPipeline
{
public:
    virtual ~IPacketPipeline() = default;

    virtual Status<PacketSize> Encode(ICodec& codec, IMessage& message, byte* buffer, PacketSize capacity) = 0;
    virtual Status<IMessage*> Decode(ICodec& codec, const byte* buffer, PacketSize size, IMessageAllocator& allocator) = 0;
};

struct PacketBuffer
{
    uint8 codec_id = 0;
    PacketSize size = 0;
    byte* bytes = nullptr;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
