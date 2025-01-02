#pragma once

#include "NetworkDefinition.hpp"
#include <Hypnos-Core/Encoding.hpp>
#include <cstring>

namespace Blanketmen {
namespace Hypnos {

class PacketReader
{
public:
    inline static void ReadInt8(PacketBuffer& buf, int8& result)
    {
        result = *reinterpret_cast<int8_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(int8);
    }

    inline static void ReadUInt8(PacketBuffer& buf, uint8& result)
    {
        result = *reinterpret_cast<uint8_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(uint8);
    }

    inline static void ReadInt16(PacketBuffer& buf, int16& result)
    {
        result = *reinterpret_cast<int16_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(int16);
    }

    inline static void ReadUInt16(PacketBuffer& buf, uint16& result)
    {
        result = *reinterpret_cast<uint16_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(uint16);
    }

    inline static void ReadInt32(PacketBuffer& buf, int32& result)
    {
        result = *reinterpret_cast<int32_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(int32);
    }

    inline static void ReadUInt32(PacketBuffer& buf, uint32& result)
    {
        result = *reinterpret_cast<uint32_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(uint32);
    }

    inline static void ReadVarint32(PacketBuffer& buf, int32& result)
    {
        result = static_cast<int32>(Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(buf.data), buf.offset));
    }

    inline static void ReadUVarint32(PacketBuffer& buf, uint32& result)
    {
        result = Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
    }

    inline static void ReadSVarint32(PacketBuffer& buf, int32& result)
    {
        result = Encoding::ZigZag::Decode(Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(buf.data), buf.offset));
    }

    inline static void ReadInt64(PacketBuffer& buf, int64& result)
    {
        result = *reinterpret_cast<int64_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(int64);
    }

    inline static void ReadUInt64(PacketBuffer& buf, uint64& result)
    {
        result = *reinterpret_cast<uint64_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(uint64);
    }

    inline static void ReadVarint64(PacketBuffer& buf, int64& result)
    {
        result = static_cast<int64>(Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8_ptr>(buf.data), buf.offset));
    }

    inline static void ReadUVarint64(PacketBuffer& buf, uint64& result)
    {
        result = Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
    }

    inline static void ReadSVarint64(PacketBuffer& buf, int64& result)
    {
        result = Encoding::ZigZag::Decode(Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8_ptr>(buf.data), buf.offset));
    }

    inline static void ReadFloat32(PacketBuffer& buf, float32& result)
    {
        result = *reinterpret_cast<float32_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(float32);
    }

    inline static void ReadFloat64(PacketBuffer& buf, float64& result)
    {
        result = *reinterpret_cast<float64_ptr>(buf.data + buf.offset);
        buf.offset += sizeof(float64);
    }

    inline static void ReadString(PacketBuffer& buf, string& result)
    {
        int32 length = Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(buf.data), buf.offset);
        result.assign(buf.data + buf.offset, length);
        buf.offset += length;
    }
};

} // namespace Hypnos
} // namespace Blanketmen