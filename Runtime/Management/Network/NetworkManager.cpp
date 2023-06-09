﻿#include <Foundation/Logging.hpp>
#include "TcpListener.hpp"
#include "UdpListener.hpp"
#include "NetworkManager.hpp"

namespace Blanketmen {
namespace Hypnos {

void NetworkManager::Initialize(NetworkConfig* config)
{
    switch (config->listenerCfg.protocol)
    {
        case TransportProtocol::TCP:
        {
            socketListener = new TcpListener(config->listenerCfg, requestHandlerMap);
            break;
        }
        case TransportProtocol::UDP :
        {
            socketListener = new UdpListener(config->listenerCfg, requestHandlerMap);
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
    delete socketListener;
}

} // namespace Hypnos
} // namespace Blanketmen