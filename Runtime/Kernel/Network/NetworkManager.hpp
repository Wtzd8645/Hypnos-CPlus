#ifndef NETWORK_MANAGER_HPP_
#define NETWORK_MANAGER_HPP_

#include <Core/Runtime/Container.hpp>
#include <Core/Runtime/Delegate.hpp>
#include "NetworkConfig.hpp"
#include "SocketListenerBase.hpp"

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
    void operator=(NetworkManager const&) = delete;
    ~NetworkManager() { }

private:
    SocketListenerBase* socketListener;
    Container::UnorderedMap<uint16, Delegate<RequestBase*>*> requestHandlerMap;

public:
    void Initialize(NetworkConfig* config);
    void Release();
    inline void Listen() { socketListener->Listen(); }
    inline void Dispatch() { socketListener->Dispatch(); }
    inline void Send(ResponseBase* response) { socketListener->Send(response); }

    template<typename TObj>
    void Register(uint16 requestId, void(TObj::* const funcPtr)(RequestBase*), TObj* const obj)
    {
        Delegate<RequestBase*>* del = requestHandlerMap[requestId];
        if (del == nullptr)
        {
            del = new Delegate<RequestBase*>(); // TODO: 刪除
            requestHandlerMap[requestId] = del;
        }
        del->Add<TObj>(funcPtr, obj);
    }

    template<typename TObj>
    void Unregister(uint16 requestId, void(TObj::* const funcPtr)(RequestBase*), TObj* const obj)
    {
        Delegate<RequestBase*>* del = requestHandlerMap[requestId];
        if (del == nullptr)
        {
            return;
        }
        del->Remove<TObj>(funcPtr, obj);
    }
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // NETWORK_MANAGER_HPP_