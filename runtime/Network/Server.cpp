#include <Hypnos/Network/Server.hpp>
#include "Connection.hpp"
#include <cassert>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

Status<void> Server::Encode(const IMessage& message, EncodedMessage& encoded) const
{
    encoded.size = 0;
    Status<PacketSize> result = message.Pack(encoded.buffer, static_cast<PacketSize>(MAX_PACKET_SIZE));
    if (result.IsFailed())
    {
        return Status<void>::Error(static_cast<ErrorCode>(result.ErrorCode()), result.Message());
    }

    if (result.Value() > MAX_PACKET_SIZE)
    {
        return Status<void>::Error(ErrorCode::InvalidFormat, "[Server] Encoded packet exceeded the fixed MTU payload capacity.");
    }

    encoded.size = result.Value();
    if (!encoded.IsValid())
    {
        encoded.size = 0;
        return Status<void>::Error(ErrorCode::InvalidFormat, "[Server] Encoded packet is empty or invalid.");
    }

    return Status<void>::Success();
}

Status<void> Server::Send(ConnectionHandle conn_handle, const IMessage& message)
{
    EncodedMessage encoded;
    Status<void> status = Encode(message, encoded);
    if (status.IsFailed())
    {
        return status;
    }
    else
    {
        return Send(conn_handle, encoded);
    }
}

ConnectionHandle Server::CreateHandle(Connection& conn) noexcept
{
    return ConnectionHandle(&conn, conn.generation.load(std::memory_order_acquire));
}

Connection* Server::ResolveHandle(ConnectionHandle conn_handle) noexcept
{
    Connection* conn = conn_handle.conn;
    if (conn == nullptr)
    {
        return nullptr;
    }

    if (conn->generation.load(std::memory_order_acquire) != conn_handle.generation)
    {
        return nullptr;
    }

    return conn;
}

void Server::Register(ConnectionEvent::Type type, Delegate<ConnectionEvent> handler)
{
    const size_t index = ConnectionEvent::ToIndex(type);
    assert(index < static_cast<size_t>(ConnectionEvent::Type::Count) && "[Server] Invalid connection event type.");
    conn_event_handlers[index] = handler;
}

void Server::Unregister(ConnectionEvent::Type type)
{
    const size_t index = ConnectionEvent::ToIndex(type);
    assert(index < static_cast<size_t>(ConnectionEvent::Type::Count) && "[Server] Invalid connection event type.");
    conn_event_handlers[index].Unregister();
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
