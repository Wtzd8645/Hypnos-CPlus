#pragma once

#include "NetworkDefinition.hpp"
#include <Hypnos-Core/Encoding.hpp>
#include <cstring>

namespace Blanketmen {
namespace Hypnos {

class PacketWriter
{
public:
    inline static void WriteInt8(PacketBuffer& buf, int8 val)
    {
        *reinterpret_cast<int8_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(int8);
    }

    inline static void WriteUInt8(PacketBuffer& buf, uint8 val)
    {
        *reinterpret_cast<uint8_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(uint8);
    }

    inline static void WriteInt16(PacketBuffer& buf, int16 val)
    {
        *reinterpret_cast<int16_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(int16);
    }

    inline static void WriteUInt16(PacketBuffer& buf, uint16 val)
    {
        *reinterpret_cast<uint16_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(uint16);
    }

    inline static void WriteInt32(PacketBuffer& buf, int32 val)
    {
        *reinterpret_cast<int32_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(int32);
    }

    inline static void WriteUInt32(PacketBuffer& buf, uint32 val)
    {
        *reinterpret_cast<uint32_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(uint32);
    }

    inline static void WriteVarint32(PacketBuffer& buf, int32 val)
    {
        Encoding::Base128Varints::WriteUInt32(static_cast<uint32>(val), reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
    }

    inline static void WriteUVarint32(PacketBuffer& buf, uint32 val)
    {
        Encoding::Base128Varints::WriteUInt32(val, reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
    }

    inline static void WriteSVarint32(PacketBuffer& buf, int32 val)
    {
        Encoding::Base128Varints::WriteUInt32(Encoding::ZigZag::Encode(val), reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
    }

    inline static void WriteInt64(PacketBuffer& buf, int64 val)
    {
        *reinterpret_cast<int64_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(int64);
    }

    inline static void WriteUInt64(PacketBuffer& buf, uint64 val)
    {
        *reinterpret_cast<uint64_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(uint64);
    }

    inline static void WriteVarint64(PacketBuffer& buf, int64 val)
    {
        Encoding::Base128Varints::WriteUInt64(static_cast<uint64>(val), reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
    }

    inline static void WriteUVarint64(PacketBuffer& buf, uint64 val)
    {
        Encoding::Base128Varints::WriteUInt64(val, reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
    }

    inline static void WriteSVarint64(PacketBuffer& buf, int64 val)
    {
        Encoding::Base128Varints::WriteUInt64(Encoding::ZigZag::Encode(val), reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
    }

    inline static void WriteFloat32(PacketBuffer& buf, float32 val)
    {
        *reinterpret_cast<float32_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(float32);
    }

    inline static void WriteFloat64(PacketBuffer& buf, float64 val)
    {
        *reinterpret_cast<float64_ptr>(buf.data + buf.offset) = val;
        buf.offset += sizeof(float64);
    }

    inline static void WriteString(PacketBuffer& buf, string& val)
    {
        uint16 length = val.length();
        Encoding::Base128Varints::WriteUInt32(length, reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
        ::memcpy(buf.data + buf.offset, val.c_str(), length);
        buf.offset += length;
    }
};

} // namespace Hypnos
} // namespace Blanketmen