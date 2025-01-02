#pragma once

#include <Hypnos/Network.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Tests {

class RequestProducer : public RequestProducerBase
{
public:
    RequestBase* Produce(PacketBuffer& src, Connection* conn) override
    {
        uint16 msgId = *reinterpret_cast<uint16_ptr>(src.data + src.offset);
        src.offset += +sizeof(msgId);
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
        request->Unpack(src);
        return request;
    }
};

void NetworkPasses()
{
    NetworkConfig* networkCfg = new NetworkConfig();
    networkCfg->listenerCfg.protocol = TransportProtocol::TCP;
    networkCfg->listenerCfg.maxConnections = 4096;
    networkCfg->listenerCfg.maxPacketBytes = 1024;
    networkCfg->listenerCfg.requestProducer = new RequestProducer();
    NetworkManager::Instance().Initialize(networkCfg);
}

} // namespace Tests
} // namespace Hypnos
} // namespace Blanketmen