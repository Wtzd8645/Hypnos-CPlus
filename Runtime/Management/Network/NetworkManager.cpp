﻿#include "Hypnos/Network/NetworkManager.hpp"
#include "Hypnos/Network/TcpListener.hpp"
#include "Hypnos/Network/UdpListener.hpp"

namespace Blanketmen {
namespace Hypnos {

void NetworkManager::Initialize(NetworkConfig* config)
{
    switch (config->listenerCfg.protocol)
    {
        case TransportProtocol::TCP:
        {
            //socketListener = new TcpListener(config->listenerCfg, requestHandlerMap);
            break;
        }
        case TransportProtocol::UDP :
        {
            //socketListener = new UdpListener(config->listenerCfg, requestHandlerMap);
            break;
        }
        default:
        {
            break;
        }
    }
}

void NetworkManager::Release()
{
    //delete socketListener;
}

void NetworkManager::Listen(SocketId sockId)
{
    servers[sockId]->Listen();
}

void NetworkManager::Shutdown(SocketId sockId)
{
    servers[sockId]->Release();
}

} // namespace Hypnos
} // namespace Blanketmen