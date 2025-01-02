#include "Hypnos/Network/NetworkUtils.hpp"
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdexcept>

namespace Blanketmen {
namespace Hypnos {

void NetworkUtils::SetNonBlocking(int sock_fd)
{
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags < 0)
    {
        throw std::runtime_error("[TcpSocket] Failed to get socket flags. Error: " + std::string(strerror(errno)));
    }

    if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        throw std::runtime_error("[TcpSocket] Failed to set socket flags. Error: " + std::string(strerror(errno)));
    }
}

void NetworkUtils::GetLocalAddress(int addr_family, sockaddr* result)
{
    ifaddrs* ifas = nullptr;
    getifaddrs(&ifas);
    for (ifaddrs* ifa = ifas; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0)
        {
            continue;
        }

        if (ifa->ifa_addr->sa_family == addr_family)
        {
            *result = *ifa->ifa_addr;
            break;
        }
    }

    if (ifas != nullptr)
    {
        freeifaddrs(ifas);
    }
}

} // namespace Hypnos
} // namespace Blanketmen