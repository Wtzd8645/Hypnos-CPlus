#ifndef TCP_LISTENER_HPP_
#define TCP_LISTENER_HPP_

#include "SocketListenerBase.hpp"

namespace Blanketmen {
namespace Hypnos {

class TcpListener : public SocketListenerBase
{
public:
    TcpListener(ConnectionListenerConfig& config, Container::UnorderedMap<uint16, Delegate<RequestBase&>*>& reqestHandlerMap);
    ~TcpListener();

    inline void Listen() override;
    inline void Dispatch() override;
    inline void Send(ResponseBase* response) override;

private:
    Container::UnorderedMap<SOCKET, Connection*> connectionMap;

    void ProcessEvents();
    void SendResponses();

    inline void Accept();
#if defined _WIN32
    inline void Receive(Connection* readState, IocpEventArgs* iocpArgs);
#elif defined __linux__
    inline void Receive(SOCKET sock);
#endif
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // TCP_LISTENER_HPP_