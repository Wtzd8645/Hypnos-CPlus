#pragma once

#include "NetworkDefinition.hpp"
#include <Hypnos-Core/Encoding.hpp>
#include <cstring>

namespace Blanketmen {
namespace Hypnos {

class PacketWriter
{
public:
    inline static void WriteInt8(char_ptr buffer, int32& offset, int8 val)
    {
        *reinterpret_cast<int8_ptr>(buffer + offset) = val;
        offset += sizeof(int8);
    }

    inline static void WriteUInt8(char_ptr buffer, int32& offset, uint8 val)
    {
        *reinterpret_cast<uint8_ptr>(buffer + offset) = val;
        offset += sizeof(uint8);
    }

    inline static void WriteInt16(char_ptr buffer, int32& offset, int16 val)
    {
        *reinterpret_cast<int16_ptr>(buffer + offset) = val;
        offset += sizeof(int16);
    }

    inline static void WriteUInt16(char_ptr buffer, int32& offset, uint16 val)
    {
        *reinterpret_cast<uint16_ptr>(buffer + offset) = val;
        offset += sizeof(uint16);
    }

    inline static void WriteInt32(char_ptr buffer, int32& offset, int32 val)
    {
        *reinterpret_cast<int32_ptr>(buffer + offset) = val;
        offset += sizeof(int32);
    }

    inline static void WriteUInt32(char_ptr buffer, int32& offset, uint32 val)
    {
        *reinterpret_cast<uint32_ptr>(buffer + offset) = val;
        offset += sizeof(uint32);
    }

    inline static void WriteVarint32(char_ptr buffer, int32& offset, int32 val)
    {
        Encoding::Base128Varints::WriteUInt32(static_cast<uint32>(val), reinterpret_cast<uint8_ptr>(buffer), offset);
    }

    inline static void WriteUVarint32(char_ptr buffer, int32& offset, uint32 val)
    {
        Encoding::Base128Varints::WriteUInt32(val, reinterpret_cast<uint8_ptr>(buffer), offset);
    }

    inline static void WriteSVarint32(char_ptr buffer, int32& offset, int32 val)
    {
        Encoding::Base128Varints::WriteUInt32(Encoding::ZigZag::Encode(val), reinterpret_cast<uint8_ptr>(buffer), offset);
    }

    inline static void WriteInt64(char_ptr buffer, int32& offset, int64 val)
    {
        *reinterpret_cast<int64_ptr>(buffer + offset) = val;
        offset += sizeof(int64);
    }

    inline static void WriteUInt64(char_ptr buffer, int32& offset, uint64 val)
    {
        *reinterpret_cast<uint64_ptr>(buffer + offset) = val;
        offset += sizeof(uint64);
    }

    inline static void WriteVarint64(char_ptr buffer, int32& offset, int64 val)
    {
        Encoding::Base128Varints::WriteUInt64(static_cast<uint64>(val), reinterpret_cast<uint8_ptr>(buffer), offset);
    }

    inline static void WriteUVarint64(char_ptr buffer, int32& offset, uint64 val)
    {
        Encoding::Base128Varints::WriteUInt64(val, reinterpret_cast<uint8_ptr>(buffer), offset);
    }

    inline static void WriteSVarint64(char_ptr buffer, int32& offset, int64 val)
    {
        Encoding::Base128Varints::WriteUInt64(Encoding::ZigZag::Encode(val), reinterpret_cast<uint8_ptr>(buffer), offset);
    }

    inline static void WriteFloat32(char_ptr buffer, int32& offset, float32 val)
    {
        *reinterpret_cast<float32_ptr>(buffer + offset) = val;
        offset += sizeof(float32);
    }

    inline static void WriteFloat64(char_ptr buffer, int32& offset, float64 val)
    {
        *reinterpret_cast<float64_ptr>(buffer + offset) = val;
        offset += sizeof(float64);
    }

    inline static void WriteString(char_ptr buffer, int32& offset, string& val)
    {
        uint16 len = val.length();
        Encoding::Base128Varints::WriteUInt32(len, reinterpret_cast<uint8_ptr>(buffer), offset);
        memcpy(buffer + offset, val.c_str(), len);
        offset += len;
    }
};

} // namespace Hypnos
} // namespace Blanketmen