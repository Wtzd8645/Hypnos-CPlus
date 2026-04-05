#pragma once

#include <Hypnos/Network/Connection.hpp>
#include <Hypnos/Network/IOContext.hpp>
#include <cassert>

namespace Blanketmen {
namespace Hypnos {
namespace Tests {

namespace {

struct CompletionTrace
{
    int call_count = 0;
    int tags[2] = { 0, 0 };
    int32 results[2] = { 0, 0 };
    uint32 flags[2] = { 0, 0 };
};

struct TaggedCompletionArgs : Network::CompletionArgs
{
    int tag = 0;
};

void RecordPrimaryCompletion(int32 res, uint32 flags, Network::CompletionArgs* args)
{
    CompletionTrace& trace = *static_cast<CompletionTrace*>(args->owner);
    TaggedCompletionArgs& tagged_args = *static_cast<TaggedCompletionArgs*>(args);
    const int call_index = trace.call_count++;
    trace.tags[call_index] = 100 + tagged_args.tag;
    trace.results[call_index] = res;
    trace.flags[call_index] = flags;
}

void RecordSecondaryCompletion(int32 res, uint32 flags, Network::CompletionArgs* args)
{
    CompletionTrace& trace = *static_cast<CompletionTrace*>(args->owner);
    TaggedCompletionArgs& tagged_args = *static_cast<TaggedCompletionArgs*>(args);
    const int call_index = trace.call_count++;
    trace.tags[call_index] = 200 + tagged_args.tag;
    trace.results[call_index] = res;
    trace.flags[call_index] = flags;
}

void TokenizedCompletionDispatchPasses()
{
    CompletionTrace trace { };

    TaggedCompletionArgs first_args { };
    first_args.complete = &RecordPrimaryCompletion;
    first_args.owner = &trace;
    first_args.rid = 1;
    first_args.tag = 1;

    TaggedCompletionArgs second_args { };
    second_args.complete = &RecordSecondaryCompletion;
    second_args.owner = &trace;
    second_args.rid = 1;
    second_args.tag = 2;

    assert(first_args.complete != nullptr && "[NetworkTests] First completion callback must be bound.");
    assert(second_args.complete != nullptr && "[NetworkTests] Second completion callback must be bound.");
    first_args.complete(11, 0, &first_args);
    second_args.complete(22, IORING_CQE_F_MORE, &second_args);

    assert(trace.call_count == 2 && "[NetworkTests] Completion callbacks should fire in dispatch order.");
    assert(trace.tags[0] == 101 && trace.results[0] == 11 && trace.flags[0] == 0 && "[NetworkTests] First completion callback should preserve its bound token.");
    assert(trace.tags[1] == 202 && trace.results[1] == 22 && trace.flags[1] == IORING_CQE_F_MORE && "[NetworkTests] Second completion callback should preserve its bound token.");
}

void ConnectionHandlePasses()
{
    Network::Connection conn { };
    conn.rid = 3;
    conn.slot = 7;
    conn.version = 5;

    const Network::ConnectionHandle conn_handle(conn);
    assert(conn_handle.IsValid() && "[NetworkTests] Connection handle should remain valid for active transport peers.");
    assert(conn_handle.rid == 3 && conn_handle.slot == 7 && conn_handle.version == 5 && "[NetworkTests] Connection handle should preserve peer identity.");

    const Network::ConnectionEvent evt { Network::ConnectionEvent::Type::Connected, conn_handle };
    assert(evt.conn_handle == conn_handle && "[NetworkTests] Connection events should continue to carry the transport peer handle.");
}

} // namespace

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
    TokenizedCompletionDispatchPasses();
    ConnectionHandlePasses();
}

} // namespace Tests
} // namespace Hypnos
} // namespace Blanketmen
