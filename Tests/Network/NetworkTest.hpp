#pragma once

#include <Hypnos/Network.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Tests {

class RequestProducer : public RequestFactoryBase
{
public:
    RequestBase* Create(char_ptr buf, Connection* conn) override
    {
        uint16 msgId = *reinterpret_cast<uint16_ptr>(buf);
        RequestBase* request;
        switch (msgId)
        {
            // case 1: request = new LoginRequest(); break;
            // case 65534: request = new ChatRequest(); break;
            // case 65535: request = new EchoRequest(); break;
            default: return nullptr;
        }

        request->conn = conn;
        request->header.msgId = msgId;
        request->Unpack(buf + sizeof(msgId));
        return request;
    }

    void Recycle(RequestBase* req) override
    {
        delete req;
    }
};

void NetworkPasses()
{
    NetworkConfig* networkCfg = new NetworkConfig();
    networkCfg->listenerCfg.protocol = TransportProtocol::TCP;
    networkCfg->listenerCfg.maxConnections = 4096;
    networkCfg->listenerCfg.maxPacketBytes = 1024;
    networkCfg->listenerCfg.request_factory = new RequestProducer();
    NetworkManager::Instance().Initialize(networkCfg);
}

} // namespace Tests
} // namespace Hypnos
} // namespace Blanketmen