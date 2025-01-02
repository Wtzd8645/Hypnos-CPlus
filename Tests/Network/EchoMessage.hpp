/* Auto-generated file */
#pragma once

#include <Hypnos/Network.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Tests {

struct EchoStruct1
{
public:
    int32 a;
    Vector<string> b;

    inline void WriteTo(PacketBuffer& buf)
    {
        PacketWriter::WriteSVarint32(buf, a);
        {
            int32 length = b.size();
            PacketWriter::WriteVarint32(buf, length);
            for (int32 i = 0; i < length; ++i)
            {
                PacketWriter::WriteString(buf, b[i]);
            }
        }
    }

    inline void ReadFrom(PacketBuffer& buf)
    {
        PacketReader::ReadSVarint32(buf, a);
        {
            int32 length;
            PacketReader::ReadVarint32(buf, length);
            b.resize(length);
            for (int32 i = 0; i < length; ++i)
            {
                PacketReader::ReadString(buf, b[i]);
            }
        }
    }
};

struct EchoStruct2
{
public:
    EchoStruct1 b;

    inline void WriteTo(PacketBuffer& buf)
    {
        b.WriteTo(buf);
    }

    inline void ReadFrom(PacketBuffer& buf)
    {
        b.ReadFrom(buf);
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
        PacketReader::ReadInt64(buffer, a);
        PacketReader::ReadVarint32(buffer, b);
        PacketReader::ReadSVarint64(buffer, c);
        PacketReader::ReadUVarint32(buffer, d);
        {
            int32 length;
            PacketReader::ReadVarint32(buffer, length);
            e.resize(length);
            for (int32 i = 0; i < length; ++i)
            {
                e[i].ReadFrom(buffer);
            }
        }
        PacketReader::ReadFloat32(buffer, f);
        PacketReader::ReadUInt8(buffer, g);
        PacketReader::ReadString(buffer, h);
        PacketReader::ReadInt8(buffer, i);
        PacketReader::ReadFloat64(buffer, j);
        PacketReader::ReadVarint64(buffer, k);
        PacketReader::ReadSVarint32(buffer, l);
        m.ReadFrom(buffer);
        PacketReader::ReadUVarint64(buffer, n);
        PacketReader::ReadInt16(buffer, o);
        PacketReader::ReadUInt64(buffer, p);
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
        PacketWriter::WriteInt64(buffer, a);
        PacketWriter::WriteVarint32(buffer, b);
        PacketWriter::WriteSVarint64(buffer, c);
        PacketWriter::WriteUVarint32(buffer, d);
        {
            int32 length = e.size();
            PacketWriter::WriteVarint32(buffer, length);
            for (int32 i = 0; i < length; ++i)
            {
                e[i].WriteTo(buffer);
            }
        }
        PacketWriter::WriteFloat32(buffer, f);
        PacketWriter::WriteUInt8(buffer, g);
        PacketWriter::WriteString(buffer, h);
        PacketWriter::WriteInt8(buffer, i);
        PacketWriter::WriteFloat64(buffer, j);
        PacketWriter::WriteVarint64(buffer, k);
        PacketWriter::WriteSVarint32(buffer, l);
        m.WriteTo(buffer);
        PacketWriter::WriteUVarint64(buffer, n);
        PacketWriter::WriteInt16(buffer, o);
        PacketWriter::WriteUInt64(buffer, p);
    }
};

} // namespace Tests
} // namespace Hypnos
} // namespace Blanketmen