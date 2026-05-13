#pragma once

#include <cstring>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct PacketWriter
{
    inline static void WriteInt8(uint8* buffer, int32& offset, int8 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(int8));
        offset += sizeof(int8);
    }

    inline static void WriteUInt8(uint8* buffer, int32& offset, uint8 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(uint8));
        offset += sizeof(uint8);
    }

    inline static void WriteInt16(uint8* buffer, int32& offset, int16 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(int16));
        offset += sizeof(int16);
    }

    inline static void WriteUInt16(uint8* buffer, int32& offset, uint16 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(uint16));
        offset += sizeof(uint16);
    }

    inline static void WriteInt32(uint8* buffer, int32& offset, int32 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(int32));
        offset += sizeof(int32);
    }

    inline static void WriteUInt32(uint8* buffer, int32& offset, uint32 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(uint32));
        offset += sizeof(uint32);
    }

    inline static void WriteVarint32(uint8* buffer, int32& offset, int32 value)
    {
        Base128Varints::WriteUInt32(static_cast<uint32>(value), buffer, offset);
    }

    inline static void WriteUVarint32(uint8* buffer, int32& offset, uint32 value)
    {
        Base128Varints::WriteUInt32(value, buffer, offset);
    }

    inline static void WriteSVarint32(uint8* buffer, int32& offset, int32 value)
    {
        Base128Varints::WriteUInt32(ZigZag::Encode(value), buffer, offset);
    }

    inline static void WriteInt64(uint8* buffer, int32& offset, int64 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(int64));
        offset += sizeof(int64);
    }

    inline static void WriteUInt64(uint8* buffer, int32& offset, uint64 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(uint64));
        offset += sizeof(uint64);
    }

    inline static void WriteVarint64(uint8* buffer, int32& offset, int64 value)
    {
        Base128Varints::WriteUInt64(static_cast<uint64>(value), buffer, offset);
    }

    inline static void WriteUVarint64(uint8* buffer, int32& offset, uint64 value)
    {
        Base128Varints::WriteUInt64(value, buffer, offset);
    }

    inline static void WriteSVarint64(uint8* buffer, int32& offset, int64 value)
    {
        Base128Varints::WriteUInt64(ZigZag::Encode(value), buffer, offset);
    }

    inline static void WriteFloat32(uint8* buffer, int32& offset, float32 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(float32));
        offset += sizeof(float32);
    }

    inline static void WriteFloat64(uint8* buffer, int32& offset, float64 value)
    {
        std::memcpy(buffer + offset, &value, sizeof(float64));
        offset += sizeof(float64);
    }

    inline static void WriteString(uint8* buffer, int32& offset, string& value)
    {
        uint32 len = value.length();
        Base128Varints::WriteUInt32(len, buffer, offset);
        std::memcpy(buffer + offset, value.c_str(), len);
        offset += len;
    }
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen