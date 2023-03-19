#ifndef NETWORK_TEST_HPP_
#define NETWORK_TEST_HPP_

#include <Kernel/Network.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Tests {

class RequestProducer : public RequestProducerBase
{
public:
    RequestBase* Produce(PacketBuffer& source, ConnectionId* connId) override
    {
        uint16 msgId = *reinterpret_cast<uint16_ptr>(source.final + source.offset);
        source.offset += +sizeof(msgId);
        RequestBase* request;
        switch (msgId)
        {
            // case 1: request = new LoginRequest(); break;
            // case 65534: request = new ChatRequest(); break;
            // case 65535: request = new EchoRequest(); break;
            default: return nullptr;
        }

        request->connId = connId;
        request->header.msgId = msgId;
        request->Unpack(source);
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
    NetworkManager::Instance()->Initialize(networkCfg);
}

} // namespace Tests
} // namespace Hypnos
} // namespace Blanketmen

#endif // NETWORK_TEST_HPP_