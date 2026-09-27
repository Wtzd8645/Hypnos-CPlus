#pragma once

#include "NetworkRuntimeDefs.hpp"
#include "PacketFraming.hpp"

#include <Hypnos-Core/Container/List.hpp>

#include <cassert>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct PacketStorage
{
    List<byte> bytes;
    uint32 size = 0;
    uint8 codec_id = 0;
    bool in_use = false;

    void Initialize(uint32 payload_capacity)
    {
        assert(payload_capacity > 0);
        assert(payload_capacity <= MAX_PACKET_PAYLOAD_SIZE);

        bytes.clear();
        bytes.resize(static_cast<size_t>(PACKET_HEADER_SIZE) + payload_capacity);
        size = 0;
        codec_id = 0;
        in_use = false;
    }
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
