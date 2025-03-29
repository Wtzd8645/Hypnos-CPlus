#pragma once

#include <Hypnos/Network.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Tests {

//class RequestProducer : public RequestFactoryBase
//{
//public:
//    RequestBase* Create(uint8* buf, Connection* conn) override
//    {
//        uint16 msgId = *reinterpret_cast<uint16*>(buf);
//        RequestBase* request;
//        switch (msgId)
//        {
//            case 1: request = new LoginRequest(); break;
//            case 65534: request = new ChatRequest(); break;
//            case 65535: request = new EchoRequest(); break;
//            default: return nullptr;
//        }
//
//        request->conn = conn;
//        request->header.msgId = msgId;
//        request->Unpack(buf + sizeof(msgId));
//        return request;
//        return nullptr;
//    }
//
//    void Recycle(RequestBase* req) override
//    {
//        delete req;
//    }
//};

void NetworkPasses()
{
    //NetworkConfig config;
    //config.listenerCfg.protocol = TransportProtocol::TCP;
    //config.listenerCfg.maxConnections = 4096;
    //config.listenerCfg.maxPacketBytes = 1024;
    //config.listenerCfg.request_factory = new RequestProducer();
    //NetworkManager::SetConfig(config);
    //NetworkManager::Instance().Initialize();
}

} // namespace Tests
} // namespace Hypnos
} // namespace Blanketmen