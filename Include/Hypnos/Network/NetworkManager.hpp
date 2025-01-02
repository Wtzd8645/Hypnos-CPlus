#pragma once

#include "NetworkConfig.hpp"
#include "NetworkDefinition.hpp"
#include "SocketServerBase.hpp"
#include <Hypnos-Core/Container.hpp>
#include <Hypnos-Core/Mediation.hpp>

namespace Blanketmen {
namespace Hypnos {

class NetworkManager : public EventDispatcher<SocketId, SocketEventId&>
{
public:
    inline static NetworkManager& Instance() noexcept
    {
        static NetworkManager instance;
        return instance;
    }

private:
    NetworkManager() { }
    NetworkManager(NetworkManager const&) = delete;
    ~NetworkManager() override { Release(); }

    void operator=(NetworkManager const&) = delete;

public:
    void Initialize(NetworkConfig* config);
    void Release();
    void Listen(SocketId id);
    void Stop(SocketId id);

    inline void Update()
    {
        for (auto& evt : events)
        {
            Dispatch(evt.sockId, evt.evtId);
        }

        for (auto& server : servers)
        {
            server->Dispatch();
        }
    }

    inline void Send(SocketId id, ResponseBase* resp)
    {
        servers[id]->Send(resp);
    }

    inline void Register(SocketId sockId, RequestId msgId, EventHandler<ResponseBase&>* handler)
    {
        servers[sockId]->Register(msgId, handler);
    }

    inline void Unregister(SocketId sockId, RequestId msgId)
    {
        servers[sockId]->Unregister(msgId);
    }

private:
    Container::Vector<ConnectionEvent> events;
    Container::Vector<SocketServerBase*> servers;
};

} // namespace Hypnos
} // namespace Blanketmen