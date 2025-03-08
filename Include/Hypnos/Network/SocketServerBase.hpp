#pragma once

#include "RequestBase.hpp"
#include "ResponseBase.hpp"
#include <Hypnos-Core/Mediation.hpp>

namespace Blanketmen {
namespace Hypnos {

class SocketServerBase : public EventDispatcher<uint16, RequestBase*>
{
protected:
    static constexpr int32 IO_RECV_BUF_GROUP = 0;

    enum class SocketOp : int8
    {
        ACPT,
        RECV,
        SEND
    };

    struct EventArg
    {
        SocketOp op;
        Connection* conn;
    };

public:
    virtual ~SocketServerBase() = default;

    virtual void Initialize() = 0;
    virtual void Release() = 0;

    virtual void Listen() = 0;
    virtual void Shutdown() = 0;
    virtual void Dispatch() = 0;
    virtual void Send(ResponseBase* resp) = 0;
};

} // namespace Hypnos
} // namespace Blanketmen