#pragma once

#include "NetworkDefs.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct IPacketCodec
{
    virtual ~IPacketCodec() = default;

    virtual bool Encode(uint8 id, const byte* src_buf, packet_size src_len, byte* dest_buf, packet_size& dest_len) = 0;
    virtual const byte* Decode(uint8 id, const byte* src_buf, packet_size src_len, packet_size& dest_len) = 0;
};

struct IMessage
{
    virtual ~IMessage() = default;

    virtual packet_size Pack(byte* buf) = 0;
    virtual void Unpack(const byte* buf, packet_size len) = 0;
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

    virtual IMessage* Decode(const byte* buf, packet_size len) = 0;
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
