#pragma once

#include "NetworkConfig.hpp"
#include "SocketListenerBase.hpp"
#include <Hypnos-Core/Container.hpp>
#include <Hypnos-Core/Delegate.hpp>

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

    SocketListenerBase* socketListener;
    Container::UnorderedMap<uint16, Delegate<RequestBase&>*> requestHandlerMap;

public:
    void Initialize(NetworkConfig* config);
    void Release();
    inline void Listen() { socketListener->Listen(); }
    inline void Dispatch() { socketListener->Dispatch(); }
    inline void Send(ResponseBase* response) { socketListener->Send(response); }

    template<typename TObj>
    void Register(uint16 reqId, typename Delegate<RequestBase&>::ObjectFunction<TObj>::ObjectPtr objPtr, typename Delegate<RequestBase&>::ObjectFunction<TObj>::MethodPtr mtdPtr)
    {
        Delegate<RequestBase&>* del = requestHandlerMap[reqId];
        if (del == nullptr)
        {
            del = new Delegate<RequestBase&>(); // TODO: Delete this delegate.
            requestHandlerMap[reqId] = del;
        }
        else
        {
            del->Add<TObj>(objPtr, mtdPtr);
        }
    }

    template<typename TObj>
    void Unregister(uint16 reqId, typename Delegate<RequestBase&>::ObjectFunction<TObj>::ObjectPtr objPtr, typename Delegate<RequestBase&>::ObjectFunction<TObj>::MethodPtr mtdPtr)
    {
        Delegate<RequestBase&>* del = requestHandlerMap[reqId];
        if (del != nullptr)
        {
            del->Remove<TObj>(objPtr, mtdPtr);
        }
    }
};

} // namespace Hypnos
} // namespace Blanketmen