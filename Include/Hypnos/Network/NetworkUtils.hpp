#pragma once

#if defined _WIN32

#elif defined __linux__
#include <sys/socket.h>
#endif

namespace Blanketmen {
namespace Hypnos {

class NetworkUtils
{
public:
#if defined _WIN32

#elif defined __linux__
    static void SetNonBlocking(int sock_fd);
    static void GetLocalAddress(int addr_family, sockaddr* result);
#endif
};

} // namespace Hypnos
} // namespace Blanketmen