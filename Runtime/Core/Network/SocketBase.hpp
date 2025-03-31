#pragma once

#include "NetworkDefs.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class SocketBase
{
public:
    static constexpr int32 BUF_META_OFFSET = Memory::AlignUp(MAX_PACKET_SIZE, alignof(BufferMetadata));

    const uint8 id;

    SocketBase(uint8 id, IOUringContext& ctx) : id(id), sock_fd(INVALID_FD), version(0), io_ctx(ctx) { }

    virtual ~SocketBase() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;

    virtual void Dispatch() = 0;
    virtual void ProcessEvent(IOEventArgs* args, int32 res, uint32 flags) = 0;

protected:
    int32 sock_fd;
    uint8 version;
    IOUringContext& io_ctx;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen