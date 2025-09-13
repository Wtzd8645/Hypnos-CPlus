#pragma once

#include <Hypnos-Core/Encoding/Base128Varints.hpp>
#include <Hypnos-Core/Encoding/ZigZag.hpp>
#include <cstring>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct PacketReader
{
    inline static void ReadInt8(uint8* buffer, int32& offset, int8& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(int8));
        offset += sizeof(int8);
    }

    inline static void ReadUInt8(uint8* buffer, int32& offset, uint8& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(uint8));
        offset += sizeof(uint8);
    }

    inline static void ReadInt16(uint8* buffer, int32& offset, int16& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(int16));
        offset += sizeof(int16);
    }

    inline static void ReadUInt16(uint8* buffer, int32& offset, uint16& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(uint16));
        offset += sizeof(uint16);
    }

    inline static void ReadInt32(uint8* buffer, int32& offset, int32& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(int32));
        offset += sizeof(int32);
    }

    inline static void ReadUInt32(uint8* buffer, int32& offset, uint32& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(uint32));
        offset += sizeof(uint32);
    }

    inline static void ReadVarint32(uint8* buffer, int32& offset, int32& result)
    {
        result = static_cast<int32>(Base128Varints::ReadUInt32(buffer, offset));
    }

    inline static void ReadUVarint32(uint8* buffer, int32& offset, uint32& result)
    {
        result = Base128Varints::ReadUInt32(buffer, offset);
    }

    inline static void ReadSVarint32(uint8* buffer, int32& offset, int32& result)
    {
        result = ZigZag::Decode(Base128Varints::ReadUInt32(buffer, offset));
    }

    inline static void ReadInt64(uint8* buffer, int32& offset, int64& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(int64));
        offset += sizeof(int64);
    }

    inline static void ReadUInt64(uint8* buffer, int32& offset, uint64& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(uint64));
        offset += sizeof(uint64);
    }

    inline static void ReadVarint64(uint8* buffer, int32& offset, int64& result)
    {
        result = static_cast<int64>(Base128Varints::ReadUInt64(buffer, offset));
    }

    inline static void ReadUVarint64(uint8* buffer, int32& offset, uint64& result)
    {
        result = Base128Varints::ReadUInt64(buffer, offset);
    }

    inline static void ReadSVarint64(uint8* buffer, int32& offset, int64& result)
    {
        result = ZigZag::Decode(Base128Varints::ReadUInt64(buffer, offset));
    }

    inline static void ReadFloat32(uint8* buffer, int32& offset, float32& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(float32));
        offset += sizeof(float32);
    }

    inline static void ReadFloat64(uint8* buffer, int32& offset, float64& result)
    {
        std::memcpy(&result, buffer + offset, sizeof(float64));
        offset += sizeof(float64);
    }

    inline static void ReadString(uint8* buffer, int32& offset, string& result)
    {
        uint32 len = Base128Varints::ReadUInt32(buffer, offset);
        result.assign(reinterpret_cast<char8*>(buffer + offset), len);
        offset += len;
    }
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen