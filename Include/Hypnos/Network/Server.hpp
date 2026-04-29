#pragma once

#include "ConnectionHandle.hpp"
#include "IMessage.hpp"
#include "NetworkDefs.hpp"
#include <Hypnos-Core/Mediation/Delegate.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct Connection;

class Server
{
public:
    virtual ~Server() = default;

    const uint8 id;
    const int32 max_conns = 0;

    virtual Status<void> Close(ConnectionHandle conn_handle) = 0;
    Status<void> Encode(const IMessage& message, EncodedMessage& encoded) const;
    Status<void> Send(ConnectionHandle conn_handle, const IMessage& message);
    virtual Status<void> Send(ConnectionHandle conn_handle, const EncodedMessage& encoded) = 0;

    void Register(ConnectionEvent::Type type, Delegate<ConnectionEvent> handler);
    void Unregister(ConnectionEvent::Type type);

protected:
    Server(int32 id, int32 max_conns) :
        id(static_cast<uint8>(id)),
        max_conns(max_conns),
        request_dispatcher(nullptr),
        request_allocator(nullptr)
    {
    }

    static ConnectionHandle CreateHandle(Connection& conn) noexcept;
    static Connection* ResolveHandle(ConnectionHandle conn_handle) noexcept;

    IMessageDispatcher* request_dispatcher;
    IMessageAllocator* request_allocator;
    Delegate<ConnectionEvent> conn_event_handlers[static_cast<size_t>(ConnectionEvent::Type::Count)];
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
