#pragma once

#include "SocketListenerBase.hpp"

namespace Blanketmen {
namespace Hypnos {

class UdpListener : public SocketListenerBase
{
public:
    UdpListener(ConnectionListenerConfig& config, Container::UnorderedMap<uint16, Delegate<RequestBase&>*>& reqHandlerDict);
    ~UdpListener();

    inline void Listen() override;
    inline void Dispatch() override;
    inline void Send(ResponseBase* response) override;

private:
    char* receiveBuf;
    Container::UnorderedMap<string, Connection*> connection_map;

    void ProcessEvents();
    void SendResponses();

    inline void Receive();
};

} // namespace Hypnos
} // namespace Blanketmen