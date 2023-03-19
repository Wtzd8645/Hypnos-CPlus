/* Auto-generated file */
#ifndef ECHOMESSAGE_HPP_
#define ECHOMESSAGE_HPP_

#include <Kernel/Network.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Tests {

struct EchoStruct1
{
public:
    int32 a;
    Vector<string> b;

    inline void WriteTo(SerializationState& state)
    {
        PacketWriter::WriteSVarint32(state, a);
        {
            int32 length = b.size();
            PacketWriter::WriteVarint32(state, length);
            for (int32 i = 0; i < length; ++i)
            {
                PacketWriter::WriteString(state, b[i]);
            }
        }
    }

    inline void ReadFrom(SerializationState& state)
    {
        PacketReader::ReadSVarint32(state, a);
        {
            int32 length;
            PacketReader::ReadVarint32(state, length);
            b.resize(length);
            for (int32 i = 0; i < length; ++i)
            {
                PacketReader::ReadString(state, b[i]);
            }
        }
    }
};

struct EchoStruct2
{
public:
    EchoStruct1 b;

    inline void WriteTo(SerializationState& state)
    {
        b.WriteTo(state);
    }

    inline void ReadFrom(SerializationState& state)
    {
        b.ReadFrom(state);
    }
};

class EchoRequest : public RequestBase
{
public:
    int64 a;
    int32 b;
    int64 c;
    uint32 d;
    Vector<EchoStruct2> e;
    float32 f;
    uint8 g;
    string h;
    int8 i;
    float64 j;
    int64 k;
    int32 l;
    EchoStruct1 m;
    uint64 n;
    int16 o;
    uint64 p;

protected:
    void UnpackBody() override
    {
        PacketReader::ReadInt64(state, a);
        PacketReader::ReadVarint32(state, b);
        PacketReader::ReadSVarint64(state, c);
        PacketReader::ReadUVarint32(state, d);
        {
            int32 length;
            PacketReader::ReadVarint32(state, length);
            e.resize(length);
            for (int32 i = 0; i < length; ++i)
            {
                e[i].ReadFrom(state);
            }
        }
        PacketReader::ReadFloat32(state, f);
        PacketReader::ReadUInt8(state, g);
        PacketReader::ReadString(state, h);
        PacketReader::ReadInt8(state, i);
        PacketReader::ReadFloat64(state, j);
        PacketReader::ReadVarint64(state, k);
        PacketReader::ReadSVarint32(state, l);
        m.ReadFrom(state);
        PacketReader::ReadUVarint64(state, n);
        PacketReader::ReadInt16(state, o);
        PacketReader::ReadUInt64(state, p);
    }
};

class EchoResponse : public ResponseBase
{
public:
    int64 a;
    int32 b;
    int64 c;
    uint32 d;
    Vector<EchoStruct2> e;
    float32 f;
    uint8 g;
    string h;
    int8 i;
    float64 j;
    int64 k;
    int32 l;
    EchoStruct1 m;
    uint64 n;
    int16 o;
    uint64 p;

    EchoResponse()
    {
        header.msgId = 65535;
    }

protected:
    void PackBody() override
    {
        PacketWriter::WriteInt64(state, a);
        PacketWriter::WriteVarint32(state, b);
        PacketWriter::WriteSVarint64(state, c);
        PacketWriter::WriteUVarint32(state, d);
        {
            int32 length = e.size();
            PacketWriter::WriteVarint32(state, length);
            for (int32 i = 0; i < length; ++i)
            {
                e[i].WriteTo(state);
            }
        }
        PacketWriter::WriteFloat32(state, f);
        PacketWriter::WriteUInt8(state, g);
        PacketWriter::WriteString(state, h);
        PacketWriter::WriteInt8(state, i);
        PacketWriter::WriteFloat64(state, j);
        PacketWriter::WriteVarint64(state, k);
        PacketWriter::WriteSVarint32(state, l);
        m.WriteTo(state);
        PacketWriter::WriteUVarint64(state, n);
        PacketWriter::WriteInt16(state, o);
        PacketWriter::WriteUInt64(state, p);
    }
};

} // namespace Tests
} // namespace Hypnos
} // namespace Blanketmen

#endif //  ECHOMESSAGE_HPP_