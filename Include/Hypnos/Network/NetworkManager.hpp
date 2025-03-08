#pragma once

#include "NetworkConfig.hpp"
#include "NetworkDefinition.hpp"
#include "SocketServerBase.hpp"
#include <Hypnos-Core/Container/List.hpp>
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
    void Listen(ServerId sockId);
    void Shutdown(ServerId sockId);

    inline void Update()
    {
        for (auto& server : servers)
        {
            server->Dispatch();
        }
    }

    inline void Send(ServerId sockId, ResponseBase* resp)
    {
        servers[sockId]->Send(resp);
    }

    inline void Register(ServerId sockId, RequestId msgId, EventHandler<RequestBase*>* handler)
    {
        servers[sockId]->Register(msgId, handler);
    }

    inline void Unregister(ServerId sockId, RequestId msgId)
    {
        servers[sockId]->Unregister(msgId);
    }

private:
    Container::List<SocketServerBase*> servers;
};

} // namespace Hypnos
} // namespace Blanketmen