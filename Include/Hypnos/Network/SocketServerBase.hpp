#pragma once

#include "RequestBase.hpp"
#include "ResponseBase.hpp"
#include "SocketBase.hpp"
#include <Hypnos-Core/Mediation/EventDispatcher.hpp>

namespace Blanketmen {
namespace Hypnos {

class SocketServerBase : public SocketBase
{
public:
    SocketServerBase(io_uring_context& ctx) : SocketBase(ctx) { }

    virtual ~SocketServerBase() = default;

    virtual void Close(Container::List<ConnectionHandle>* conn_handles) = 0;
    virtual void Send(Container::List<ConnectionHandle>* conns, ResponseBase* resp) = 0;

    virtual void Register(int32 gid, EventHandlerBase<RequestBase*>* handler) = 0;
    virtual void Unregister(int32 gid) = 0;
};

} // namespace Hypnos
} // namespace Blanketmen