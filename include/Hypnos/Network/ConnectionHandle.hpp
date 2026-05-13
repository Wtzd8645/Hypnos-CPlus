#pragma once

#include "NetworkDefs.hpp"
#include <cstddef>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

struct Connection;
class Server;

struct ConnectionHandle
{
    ConnectionHandle() = default;

    bool IsValid() const noexcept
    {
        return conn != nullptr && generation != 0;
    }

private:
    friend class Server;
    friend bool operator==(const ConnectionHandle& lhs, const ConnectionHandle& rhs) noexcept = default;

    ConnectionHandle(Connection* conn, uint32 generation) :
        conn(conn),
        generation(generation)
    {
    }

    Connection* conn = nullptr;
    uint32 generation = 0;
};

struct ConnectionEvent
{
    enum class Type : uint8
    {
        Connected,
        Disconnected,
        Count
    };

    static size_t ToIndex(Type type) noexcept
    {
        return static_cast<size_t>(type);
    }

    Type type = Type::Connected;
    ConnectionHandle handle;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
