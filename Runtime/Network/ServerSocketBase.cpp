#include "Hypnos/Network/IOContext.hpp"
#include "Hypnos/Network/ServerSocketBase.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

ServerSocketBase::ServerSocketBase(SocketConfig& cfg, IOContext& ctx) : SocketBase(cfg, ctx),
    conntions(cfg.max_conns),
    sock_events(cfg.max_conns),
    requests(cfg.max_conns),
    request_allocator(cfg.request_allocator),
    response_args(cfg.max_conns),
    response_allocator(cfg.response_allocator)
{
}

void ServerSocketBase::Register(int32 gid, IEventHandler<RequestBase*>* handler)
{
    if (gid < 0 || gid >= ServerSocketEvent::MAX_EVENT_TYPES)
    {
        Logging::Error("[ServerSocketBase] Invalid GID: %d", gid);
        return;
    }

    request_handlers[gid] = handler;
}

void ServerSocketBase::Unregister(int32 gid)
{
    if (gid < 0 || gid >= ServerSocketEvent::MAX_EVENT_TYPES)
    {
        Logging::Error("[ServerSocketBase] Invalid GID: %d", gid);
        return;
    }

    request_handlers[gid] = nullptr;
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen