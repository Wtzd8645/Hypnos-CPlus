#pragma once

#include "NetworkConfig.hpp"
#include "NetworkDefinition.hpp"
#include "SocketServerBase.hpp"
#include <Hypnos-Core/Container/List.hpp>
#include <Hypnos-Core/Mediation/EventHandlerBase.hpp>

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

    inline void Send(ServerId id, Container::List<ConnectionHandle>* conn_handles, ResponseBase* resp)
    {
        servers[id]->Send(conn_handles, resp);
    }

    inline void Register(ServerId id, RequestId gid, EventHandlerBase<RequestBase*>* handler)
    {
        servers[id]->Register(gid, handler);
    }

    inline void Unregister(ServerId id, RequestId gid)
    {
        servers[id]->Unregister(gid);
    }

private:
    Container::List<SocketServerBase*> servers;
};

} // namespace Hypnos
} // namespace Blanketmen