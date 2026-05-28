#pragma once

#include <Hypnos/Network/NetworkDefs.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

constexpr int32 INVALID_FD = -1;
constexpr uint32 INVALID_SLOT = static_cast<uint32>(~0u);

inline Status<void> NetworkError(NetworkStatus status, const char* message)
{
    return Status<void>::Error(ToErrorCode(status), message);
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
