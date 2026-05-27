#pragma once

#include "Server.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class Client
{
public:
    Client() = default;

    Status<void> Register(MessageHandler handler);
    Status<void> Register(ConnectionEventHandler handler);
    Status<void> Register(ErrorHandler handler);

    Status<void> Connect();
    Status<void> Disconnect();
    Status<void> Send(IMessage& message);
    ClientState State() const noexcept;
    EndpointId Id() const noexcept;

private:
    friend class NetworkManager;

    explicit Client(void* endpoint);

    void* endpoint = nullptr;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
