#pragma once

#include "Network.hpp"
#include <Hypnos-Kernel.hpp>
#include <cstring>

namespace Blanketmen {
namespace Hypnos {

template<typename T>
inline bool Read(const uint8_t* buffer, size_t size, size_t& offset, T& out)
{
    static_assert(std::is_trivially_copyable<T>::value, "Read<T>: T must be trivially copyable");

    if (offset + sizeof(T) > size)
    {
        return false;
    }

    std::memcpy(&out, buffer + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

class PacketReader
{
public:
    inline static void ReadInt8(uint8* buffer, int32& offset, int8& result)
    {
        result = *reinterpret_cast<int8*>(buffer + offset);
        offset += sizeof(int8);
    }

    inline static void ReadUInt8(uint8* buffer, int32& offset, uint8& result)
    {
        result = *reinterpret_cast<uint8*>(buffer + offset);
        offset += sizeof(uint8);
    }

    inline static void ReadInt16(uint8* buffer, int32& offset, int16& result)
    {
        result = *reinterpret_cast<int16*>(buffer + offset);
        offset += sizeof(int16);
    }

    inline static void ReadUInt16(uint8* buffer, int32& offset, uint16& result)
    {
        result = *reinterpret_cast<uint16*>(buffer + offset);
        offset += sizeof(uint16);
    }

    inline static void ReadInt32(uint8* buffer, int32& offset, int32& result)
    {
        result = *reinterpret_cast<int32*>(buffer + offset);
        offset += sizeof(int32);
    }

    inline static void ReadUInt32(uint8* buffer, int32& offset, uint32& result)
    {
        result = *reinterpret_cast<uint32*>(buffer + offset);
        offset += sizeof(uint32);
    }

    inline static void ReadVarint32(uint8* buffer, int32& offset, int32& result)
    {
        result = static_cast<int32>(Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8*>(buffer), offset));
    }

    inline static void ReadUVarint32(uint8* buffer, int32& offset, uint32& result)
    {
        result = Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8*>(buffer), offset);
    }

    inline static void ReadSVarint32(uint8* buffer, int32& offset, int32& result)
    {
        result = Encoding::ZigZag::Decode(Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8*>(buffer), offset));
    }

    inline static void ReadInt64(uint8* buffer, int32& offset, int64& result)
    {
        result = *reinterpret_cast<int64*>(buffer + offset);
        offset += sizeof(int64);
    }

    inline static void ReadUInt64(uint8* buffer, int32& offset, uint64& result)
    {
        result = *reinterpret_cast<uint64*>(buffer + offset);
        offset += sizeof(uint64);
    }

    inline static void ReadVarint64(uint8* buffer, int32& offset, int64& result)
    {
        result = static_cast<int64>(Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8*>(buffer), offset));
    }

    inline static void ReadUVarint64(uint8* buffer, int32& offset, uint64& result)
    {
        result = Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8*>(buffer), offset);
    }

    inline static void ReadSVarint64(uint8* buffer, int32& offset, int64& result)
    {
        result = Encoding::ZigZag::Decode(Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8*>(buffer), offset));
    }

    inline static void ReadFloat32(uint8* buffer, int32& offset, float32& result)
    {
        result = *reinterpret_cast<float32*>(buffer + offset);
        offset += sizeof(float32);
    }

    inline static void ReadFloat64(uint8* buffer, int32& offset, float64& result)
    {
        result = *reinterpret_cast<float64*>(buffer + offset);
        offset += sizeof(float64);
    }

    inline static void ReadString(uint8* buffer, int32& offset, string& result)
    {
        uint32 len = Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8*>(buffer), offset);
        result.assign(reinterpret_cast<char8*>(buffer + offset), len);
        offset += len;
    }
};

} // namespace Hypnos
} // namespace Blanketmen