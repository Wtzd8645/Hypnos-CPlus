#ifndef PACKET_READER_HPP_
#define PACKET_READER_HPP_

#include <cstring>
#include <Core/Runtime/Encoding.hpp>
#include "NetworkDefinition.hpp"

namespace Blanketmen {
namespace Hypnos {

class PacketReader
{
public:
    inline static void ReadInt8(SerializationState& state, int8& result)
    {
        result = *reinterpret_cast<int8_ptr>(state.buffer + state.offset);
        state.offset += sizeof(int8);
    }

    inline static void ReadUInt8(SerializationState& state, uint8& result)
    {
        result = *reinterpret_cast<uint8_ptr>(state.buffer + state.offset);
        state.offset += sizeof(uint8);
    }

    inline static void ReadInt16(SerializationState& state, int16& result)
    {
        result = *reinterpret_cast<int16_ptr>(state.buffer + state.offset);
        state.offset += sizeof(int16);
    }

    inline static void ReadUInt16(SerializationState& state, uint16& result)
    {
        result = *reinterpret_cast<uint16_ptr>(state.buffer + state.offset);
        state.offset += sizeof(uint16);
    }

    inline static void ReadInt32(SerializationState& state, int32& result)
    {
        result = *reinterpret_cast<int32_ptr>(state.buffer + state.offset);
        state.offset += sizeof(int32);
    }

    inline static void ReadUInt32(SerializationState& state, uint32& result)
    {
        result = *reinterpret_cast<uint32_ptr>(state.buffer + state.offset);
        state.offset += sizeof(uint32);
    }

    inline static void ReadVarint32(SerializationState& state, int32& result)
    {
        result = static_cast<int32>(Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(state.buffer), state.offset));
    }

    inline static void ReadUVarint32(SerializationState& state, uint32& result)
    {
        result = Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
    }

    inline static void ReadSVarint32(SerializationState& state, int32& result)
    {
        result = Encoding::ZigZag::Decode(Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(state.buffer), state.offset));
    }

    inline static void ReadInt64(SerializationState& state, int64& result)
    {
        result = *reinterpret_cast<int64_ptr>(state.buffer + state.offset);
        state.offset += sizeof(int64);
    }

    inline static void ReadUInt64(SerializationState& state, uint64& result)
    {
        result = *reinterpret_cast<uint64_ptr>(state.buffer + state.offset);
        state.offset += sizeof(uint64);
    }

    inline static void ReadVarint64(SerializationState& state, int64& result)
    {
        result = static_cast<int64>(Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8_ptr>(state.buffer), state.offset));
    }

    inline static void ReadUVarint64(SerializationState& state, uint64& result)
    {
        result = Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
    }

    inline static void ReadSVarint64(SerializationState& state, int64& result)
    {
        result = Encoding::ZigZag::Decode(Encoding::Base128Varints::ReadUInt64(reinterpret_cast<uint8_ptr>(state.buffer), state.offset));
    }

    inline static void ReadFloat32(SerializationState& state, float32& result)
    {
        result = *reinterpret_cast<float32_ptr>(state.buffer + state.offset);
        state.offset += sizeof(float32);
    }

    inline static void ReadFloat64(SerializationState& state, float64& result)
    {
        result = *reinterpret_cast<float64_ptr>(state.buffer + state.offset);
        state.offset += sizeof(float64);
    }

    inline static void ReadString(SerializationState& state, string& result)
    {
        int32 length = Encoding::Base128Varints::ReadUInt32(reinterpret_cast<uint8_ptr>(state.buffer), state.offset);
        result.assign(state.buffer + state.offset, length);
        state.offset += length;
    }
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // PACKET_READER_HPP_