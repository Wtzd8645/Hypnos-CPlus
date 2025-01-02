#pragma once

#include "NetworkDefinition.hpp"
#include <Hypnos-Core/Encoding.hpp>
#include <cstring>

namespace Blanketmen {
namespace Hypnos {

class PacketReader
{
public:
    inline static void ReadInt8(char_ptr buffer, int32& offset, int8& result)
    {
        result = *reinterpret_cast<int8_ptr>(buffer + offset);
        offset += sizeof(int8);
    }

    inline static void ReadUInt8(char_ptr buffer, int32& offset, uint8& result)
    {
        result = *reinterpret_cast<uint8_ptr>(buffer + offset);
        offset += sizeof(uint8);
    }

    inline static void ReadInt16(char_ptr buffer, int32& offset, int16& result)
    {
        result = *reinterpret_cast<int16_ptr>(buffer + offset);
        offset += sizeof(int16);
    }

    inline static void ReadUInt16(char_ptr buffer, int32& offset, uint16& result)
    {
        result = *reinterpret_cast<uint16_ptr>(buffer + offset);
        offset += sizeof(uint16);
    }

    inline static void ReadInt32(char_ptr buffer, int32& offset, int32& result)
    {
        result = *reinterpret_cast<int32_ptr>(buffer + offset);
        offset += sizeof(int32);
    }

    inline static void ReadUInt32(char_ptr buffer, int32& offset, uint32& result)
    {
        result = *reinterpret_cast<uint32_ptr>(buffer + offset);
        offset += sizeof(uint32);
    }

    inline static void ReadVarint32(char_ptr buffer, int32& offset, int32& result)
    {
        result = static_cast<int32>(Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(buffer), offset));
    }

    inline static void ReadUVarint32(char_ptr buffer, int32& offset, uint32& result)
    {
        result = Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(buffer), offset);
    }

    inline static void ReadSVarint32(char_ptr buffer, int32& offset, int32& result)
    {
        result = Encoding::ZigZag::Decode(Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(buffer), offset));
    }

    inline static void ReadInt64(char_ptr buffer, int32& offset, int64& result)
    {
        result = *reinterpret_cast<int64_ptr>(buffer + offset);
        offset += sizeof(int64);
    }

    inline static void ReadUInt64(char_ptr buffer, int32& offset, uint64& result)
    {
        result = *reinterpret_cast<uint64_ptr>(buffer + offset);
        offset += sizeof(uint64);
    }

    inline static void ReadVarint64(char_ptr buffer, int32& offset, int64& result)
    {
        result = static_cast<int64>(Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8_ptr>(buffer), offset));
    }

    inline static void ReadUVarint64(char_ptr buffer, int32& offset, uint64& result)
    {
        result = Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8_ptr>(buffer), offset);
    }

    inline static void ReadSVarint64(char_ptr buffer, int32& offset, int64& result)
    {
        result = Encoding::ZigZag::Decode(Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8_ptr>(buffer), offset));
    }

    inline static void ReadFloat32(char_ptr buffer, int32& offset, float32& result)
    {
        result = *reinterpret_cast<float32_ptr>(buffer + offset);
        offset += sizeof(float32);
    }

    inline static void ReadFloat64(char_ptr buffer, int32& offset, float64& result)
    {
        result = *reinterpret_cast<float64_ptr>(buffer + offset);
        offset += sizeof(float64);
    }

    inline static void ReadString(char_ptr buffer, int32& offset, string& result)
    {
        int32 len = Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(buffer), offset);
        result.assign(buffer + offset, len);
        offset += len;
    }
};

} // namespace Hypnos
} // namespace Blanketmen