#pragma once

#include "Connection.hpp"
#include "NetworkConfig.hpp"
#include "NetworkDefs.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct ServerSocketEvent
{
    enum class Type : uint8
    {
        Connect,
        Disconnect
    };

    static constexpr size_t MAX_EVENT_TYPES = static_cast<size_t>(Type::Disconnect) + 1;

    Type type;
    ConnectionHandle conn_handle;
};

class SocketBase
{
public:
    static constexpr int32 BUF_META_OFFSET = MemoryUtils::AlignUp(MAX_PACKET_SIZE, alignof(BufferMetadata));

    const uint8 id;

    SocketBase(SocketConfig& cfg, IOContext& ctx) : id(cfg.id), sock_fd(INVALID_FD), version(0), io_ctx(ctx) { }

    virtual ~SocketBase() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;

    virtual void Dispatch() = 0;
    virtual void ProcessIOEvent(IOEventArgs* args, int32 res, uint32 flags) = 0;

protected:
    int32 sock_fd;
    uint8 version;
    IOContext& io_ctx;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen