#pragma once

#include "Hypnos/Network/NetworkDefinition.hpp"

namespace Blanketmen {
namespace Hypnos {

class SocketBase
{
public:
    static constexpr int32 BUF_META_OFFSET = Memory::AlignUp(MAX_PACKET_SIZE, alignof(buffer_metadata));

    SocketBase(io_uring_context& ctx) : sock_fd(INVALID_FD), version(0), io_ctx(ctx) { }

    virtual ~SocketBase() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Dispatch() = 0;
    virtual void ProcessEvent(io_event_args* args, int32 res, uint32 flags) = 0;

protected:
    int32 sock_fd;
    int8 version;

    io_uring_context& io_ctx;
};

} // namespace Hypnos
} // namespace Blanketmen