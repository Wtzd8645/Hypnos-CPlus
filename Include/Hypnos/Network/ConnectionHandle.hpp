#pragma once

#include "NetworkDefs.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct ConnectionHandle
{
    ConnectionHandle() = default;

    ConnectionHandle(EndpointId endpoint_id, uint32 slot, uint32 generation) :
        endpoint_id(endpoint_id),
        slot(slot),
        generation(generation)
    {
    }

    bool IsValid() const noexcept
    {
        return endpoint_id != INVALID_ENDPOINT_ID && generation != 0;
    }

    EndpointId Endpoint() const noexcept { return endpoint_id; }
    uint32 Slot() const noexcept { return slot; }
    uint32 Generation() const noexcept { return generation; }

    friend bool operator==(const ConnectionHandle& lhs, const ConnectionHandle& rhs) noexcept = default;

private:
    EndpointId endpoint_id = INVALID_ENDPOINT_ID;
    uint32 slot = 0;
    uint32 generation = 0;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
