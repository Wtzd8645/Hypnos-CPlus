#pragma once

#include "NetworkDefs.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct IPacketCodec
{
    virtual ~IPacketCodec() = default;

    virtual bool Encode(uint8 id, const byte* src_buf, PacketSize src_len, byte* dest_buf, PacketSize& dest_len) = 0;
    virtual const byte* Decode(uint8 id, const byte* src_buf, PacketSize src_len, PacketSize& dest_len) = 0;
};

struct IMessage
{
    virtual ~IMessage() = default;

    virtual Status<PacketSize> Pack(byte* buf, PacketSize capacity) const = 0;
    virtual void Unpack(const byte* buf, PacketSize len) = 0;
};

struct IMessageAllocator
{
    virtual ~IMessageAllocator() = default;

    virtual IMessage* Acquire(uint32 id) = 0;
    virtual IMessage* Acquire(const byte* buf) = 0;
    virtual void Release(IMessage* msg) = 0;
};

struct IMessageCodec
{
    virtual ~IMessageCodec() = default;

    virtual IMessage* Decode(const byte* buf, PacketSize len) = 0;
    virtual void Release(IMessage* msg) = 0;
};

struct IMessageDispatcher
{
    virtual ~IMessageDispatcher() = default;

    virtual void Dispatch(const IMessage& msg) = 0;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
