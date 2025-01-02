#pragma once

#include "NetworkConfig.hpp"
#include "NetworkDefinition.hpp"
#include "SocketServerBase.hpp"
#include <Hypnos-Core/Container.hpp>
#include <Hypnos-Core/Threads/MPSC/RingBuffer.hpp>
#include <Hypnos-Core/Mediation.hpp>

namespace Blanketmen {
namespace Hypnos {

class NetworkManager
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
    ~NetworkManager() { Release(); }

public:
    void Initialize(NetworkConfig* config);
    void Release();
    void Listen(SocketId sockId);
    void Shutdown(SocketId sockId);

    inline void Update()
    {
        for (auto& server : servers)
        {
            server->Dispatch();
        }
    }

    inline void Send(SocketId sockId, ResponseBase* resp)
    {
        servers[sockId]->Send(resp);
    }

    inline void Register(SocketId sockId, RequestId msgId, EventHandler<RequestBase*>* handler)
    {
        servers[sockId]->Register(msgId, handler);
    }

    inline void Unregister(SocketId sockId, RequestId msgId)
    {
        servers[sockId]->Unregister(msgId);
    }

private:
    Container::Vector<SocketServerBase*> servers;
};

} // namespace Hypnos
} // namespace Blanketmen