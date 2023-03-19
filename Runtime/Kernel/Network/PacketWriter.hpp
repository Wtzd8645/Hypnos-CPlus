#ifndef PACKET_WRITER_HPP_
#define PACKET_WRITER_HPP_

#include <cstring>
#include <Core/Runtime/Encoding.hpp>
#include "NetworkDefinition.hpp"

namespace Blanketmen {
namespace Hypnos {

class PacketWriter
{
public:
    inline static void WriteInt8(SerializationState& state, int8 value)
    {
        *reinterpret_cast<int8_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(int8);
    }

    inline static void WriteUInt8(SerializationState& state, uint8 value)
    {
        *reinterpret_cast<uint8_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(uint8);
    }

    inline static void WriteInt16(SerializationState& state, int16 value)
    {
        *reinterpret_cast<int16_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(int16);
    }

    inline static void WriteUInt16(SerializationState& state, uint16 value)
    {
        *reinterpret_cast<uint16_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(uint16);
    }

    inline static void WriteInt32(SerializationState& state, int32 value)
    {
        *reinterpret_cast<int32_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(int32);
    }

    inline static void WriteUInt32(SerializationState& state, uint32 value)
    {
        *reinterpret_cast<uint32_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(uint32);
    }

    inline static void WriteVarint32(SerializationState& state, int32 value)
    {
        Encoding::Base128Varints::WriteUInt32(static_cast<uint32>(value), reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
    }

    inline static void WriteUVarint32(SerializationState& state, uint32 value)
    {
        Encoding::Base128Varints::WriteUInt32(value, reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
    }

    inline static void WriteSVarint32(SerializationState& state, int32 value)
    {
        Encoding::Base128Varints::WriteUInt32(Encoding::ZigZag::Encode(value), reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
    }

    inline static void WriteInt64(SerializationState& state, int64 value)
    {
        *reinterpret_cast<int64_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(int64);
    }

    inline static void WriteUInt64(SerializationState& state, uint64 value)
    {
        *reinterpret_cast<uint64_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(uint64);
    }

    inline static void WriteVarint64(SerializationState& state, int64 value)
    {
        Encoding::Base128Varints::WriteUInt64(static_cast<uint64>(value), reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
    }

    inline static void WriteUVarint64(SerializationState& state, uint64 value)
    {
        Encoding::Base128Varints::WriteUInt64(value, reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
    }

    inline static void WriteSVarint64(SerializationState& state, int64 value)
    {
        Encoding::Base128Varints::WriteUInt64(Encoding::ZigZag::Encode(value), reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
    }

    inline static void WriteFloat32(SerializationState& state, float32 value)
    {
        *reinterpret_cast<float32_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(float32);
    }

    inline static void WriteFloat64(SerializationState& state, float64 value)
    {
        *reinterpret_cast<float64_ptr>(state.buffer + state.offset) = value;
        state.offset += sizeof(float64);
    }

    inline static void WriteString(SerializationState& state, string& value)
    {
        uint16 length = value.length();
        Encoding::Base128Varints::WriteUInt32(length, reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
        ::memcpy(state.buffer + state.offset, value.c_str(), length);
        state.offset += length;
    }
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // PACKET_WRITER_HPP_