#ifndef NETWORK_UTIL_HPP_
#define NETWORK_UTIL_HPP_

#if defined _WIN32

#elif defined __linux__
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#endif

namespace Blanketmen {
namespace Hypnos {

class NetworkUtil
{
public:
#if defined _WIN32

#elif defined __linux__
    static void GetLocalAddress(int addressFamily, sockaddr* result)
    {
        ifaddrs* ifas = nullptr;
        ::getifaddrs(&ifas);
        for (ifaddrs* ifa = ifas; ifa != nullptr; ifa = ifa->ifa_next)
        {
            if ((ifa->ifa_flags & IFF_LOOPBACK) != 0)
            {
                continue;
            }

            if (ifa->ifa_addr->sa_family == addressFamily)
            {
                *result = *ifa->ifa_addr;
                break;
            }
        }

        if (ifas != nullptr)
        {
            ::freeifaddrs(ifas);
        }
    }
#endif
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // NETWORK_UTIL_HPP_