#pragma once

#include "NetworkRuntimeDefs.hpp"

#include <Hypnos/Network/NetworkConfig.hpp>

#if defined(__linux__)
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace Blanketmen {
namespace Hypnos {
namespace Network {

#if defined(__linux__)
int32 SetNonBlocking(int32 fd);
bool FillAddress(const EndpointAddress& address, sockaddr_storage& storage, socklen_t& length);
Status<int32> OpenListenSocket(const EndpointAddress& address);
Status<int32> OpenClientSocket(const EndpointAddress& address, bool& is_pending);
#endif

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
