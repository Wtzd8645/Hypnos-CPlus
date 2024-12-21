#pragma once

#include <fcntl.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/unistd.h>

namespace Blanketmen {
namespace Hypnos {

class SocketBase
{
public:
    SocketBase() = default;
    virtual ~SocketBase() = default;

    virtual void Listen() = 0;
    virtual void Connect() = 0;
    virtual void Disconnect() = 0;
    virtual void Receive() = 0;
    virtual void Send() = 0;

protected:
    int sock_fd;
};

} // namespace Hypnos
} // namespace Blanketmen