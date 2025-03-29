#pragma once

#include "Network.hpp"
#include <Hypnos-Kernel.hpp>
#include <cstring>

namespace Blanketmen {
namespace Hypnos {

class PacketWriter
{
public:
    inline static void WriteInt8(uint8* buffer, int32& offset, int8 value)
    {
        *reinterpret_cast<int8*>(buffer + offset) = value;
        offset += sizeof(int8);
    }

    inline static void WriteUInt8(uint8* buffer, int32& offset, uint8 value)
    {
        *reinterpret_cast<uint8*>(buffer + offset) = value;
        offset += sizeof(uint8);
    }

    inline static void WriteInt16(uint8* buffer, int32& offset, int16 value)
    {
        *reinterpret_cast<int16*>(buffer + offset) = value;
        offset += sizeof(int16);
    }

    inline static void WriteUInt16(uint8* buffer, int32& offset, uint16 value)
    {
        *reinterpret_cast<uint16*>(buffer + offset) = value;
        offset += sizeof(uint16);
    }

    inline static void WriteInt32(uint8* buffer, int32& offset, int32 value)
    {
        *reinterpret_cast<int32*>(buffer + offset) = value;
        offset += sizeof(int32);
    }

    inline static void WriteUInt32(uint8* buffer, int32& offset, uint32 value)
    {
        *reinterpret_cast<uint32*>(buffer + offset) = value;
        offset += sizeof(uint32);
    }

    inline static void WriteVarint32(uint8* buffer, int32& offset, int32 value)
    {
        Encoding::Base128Varints::WriteUInt32(static_cast<uint32>(value), reinterpret_cast<uint8*>(buffer), offset);
    }

    inline static void WriteUVarint32(uint8* buffer, int32& offset, uint32 value)
    {
        Encoding::Base128Varints::WriteUInt32(value, reinterpret_cast<uint8*>(buffer), offset);
    }

    inline static void WriteSVarint32(uint8* buffer, int32& offset, int32 value)
    {
        Encoding::Base128Varints::WriteUInt32(Encoding::ZigZag::Encode(value), reinterpret_cast<uint8*>(buffer), offset);
    }

    inline static void WriteInt64(uint8* buffer, int32& offset, int64 value)
    {
        *reinterpret_cast<int64*>(buffer + offset) = value;
        offset += sizeof(int64);
    }

    inline static void WriteUInt64(uint8* buffer, int32& offset, uint64 value)
    {
        *reinterpret_cast<uint64*>(buffer + offset) = value;
        offset += sizeof(uint64);
    }

    inline static void WriteVarint64(uint8* buffer, int32& offset, int64 value)
    {
        Encoding::Base128Varints::WriteUInt64(static_cast<uint64>(value), reinterpret_cast<uint8*>(buffer), offset);
    }

    inline static void WriteUVarint64(uint8* buffer, int32& offset, uint64 value)
    {
        Encoding::Base128Varints::WriteUInt64(value, reinterpret_cast<uint8*>(buffer), offset);
    }

    inline static void WriteSVarint64(uint8* buffer, int32& offset, int64 value)
    {
        Encoding::Base128Varints::WriteUInt64(Encoding::ZigZag::Encode(value), reinterpret_cast<uint8*>(buffer), offset);
    }

    inline static void WriteFloat32(uint8* buffer, int32& offset, float32 value)
    {
        *reinterpret_cast<float32*>(buffer + offset) = value;
        offset += sizeof(float32);
    }

    inline static void WriteFloat64(uint8* buffer, int32& offset, float64 value)
    {
        *reinterpret_cast<float64*>(buffer + offset) = value;
        offset += sizeof(float64);
    }

    inline static void WriteString(uint8* buffer, int32& offset, string& value)
    {
        uint32 len = value.length();
        Encoding::Base128Varints::WriteUInt32(len, reinterpret_cast<uint8*>(buffer), offset);
        std::memcpy(buffer + offset, value.c_str(), len);
        offset += len;
    }
};

} // namespace Hypnos
} // namespace Blanketmen