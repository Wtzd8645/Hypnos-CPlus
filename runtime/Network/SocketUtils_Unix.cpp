#include "NetworkCore.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

#if defined(__linux__)
int32 SetNonBlocking(int32 fd)
{
    int32 flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

Status<void> FillAddress(const EndpointAddress& address, sockaddr_storage& storage, socklen_t& length)
{
    std::memset(&storage, 0, sizeof(storage));

    sockaddr_in ipv4 { };
    ipv4.sin_family = AF_INET;
    ipv4.sin_port = htons(address.port);
    if (inet_pton(AF_INET, address.host.c_str(), &ipv4.sin_addr) == 1)
    {
        std::memcpy(&storage, &ipv4, sizeof(ipv4));
        length = sizeof(ipv4);
        return Status<void>::Success();
    }

    sockaddr_in6 ipv6 { };
    ipv6.sin6_family = AF_INET6;
    ipv6.sin6_port = htons(address.port);
    if (inet_pton(AF_INET6, address.host.c_str(), &ipv6.sin6_addr) == 1)
    {
        std::memcpy(&storage, &ipv6, sizeof(ipv6));
        length = sizeof(ipv6);
        return Status<void>::Success();
    }

    return NetworkError(NetworkStatus::InvalidConfig, "[EpollBackend] Endpoint address must be a numeric IPv4 or IPv6 address.");
}

Status<int32> OpenListenSocket(const EndpointAddress& address)
{
    sockaddr_storage storage { };
    socklen_t length = 0;
    Status<void> address_status = FillAddress(address, storage, length);
    if (address_status.IsFailed())
    {
        return Status<int32>::Error(static_cast<ErrorCode>(address_status.ErrorCode()), address_status.Message());
    }

    int32 fd = socket(storage.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == INVALID_FD)
    {
        return Status<int32>::Error(ToErrorCode(NetworkStatus::TransportError), "[EpollBackend] Failed to create listen socket.");
    }

    int32 enable = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    if (bind(fd, reinterpret_cast<sockaddr*>(&storage), length) != 0)
    {
        close(fd);
        return Status<int32>::Error(ToErrorCode(NetworkStatus::TransportError), "[EpollBackend] Failed to bind listen socket.");
    }

    if (listen(fd, SOMAXCONN) != 0)
    {
        close(fd);
        return Status<int32>::Error(ToErrorCode(NetworkStatus::TransportError), "[EpollBackend] Failed to listen on socket.");
    }

    return Status<int32>::Success(fd);
}

Status<int32> OpenClientSocket(const EndpointAddress& address, bool& is_pending)
{
    sockaddr_storage storage { };
    socklen_t length = 0;
    Status<void> address_status = FillAddress(address, storage, length);
    if (address_status.IsFailed())
    {
        return Status<int32>::Error(static_cast<ErrorCode>(address_status.ErrorCode()), address_status.Message());
    }

    int32 fd = socket(storage.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == INVALID_FD)
    {
        return Status<int32>::Error(ToErrorCode(NetworkStatus::TransportError), "[EpollBackend] Failed to create client socket.");
    }

    is_pending = false;
    if (connect(fd, reinterpret_cast<sockaddr*>(&storage), length) != 0)
    {
        if (errno != EINPROGRESS)
        {
            close(fd);
            return Status<int32>::Error(ToErrorCode(NetworkStatus::TransportError), "[EpollBackend] Failed to connect client socket.");
        }

        is_pending = true;
    }

    return Status<int32>::Success(fd);
}

#endif

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
