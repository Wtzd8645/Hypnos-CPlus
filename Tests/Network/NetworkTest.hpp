#pragma once

#include <Hypnos/Network/NetworkManager.hpp>
#include "../../Runtime/Network/Connection.hpp"
#include "../../Runtime/Network/EndpointRuntime.hpp"
#include "../../Runtime/Network/NetworkShard.hpp"
#include <array>
#include <cassert>
#include <sys/mman.h>
#include <thread>

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

class OwnerThreadProbeEndpoint : public Network::EndpointRuntime
{
public:
    OwnerThreadProbeEndpoint() : EndpointRuntime(0, EndpointRuntime::Type::Server) { }

    Status<void> Start() override { return Status<void>::Success(); }
    Status<void> Stop() override { return Status<void>::Success(); }
    void Dispatch() override { }

    bool ProbeOwnerThread() { return ClaimOwnerThread(); }
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
    first_args.type = Network::CompletionArgs::Type::Wake;
    first_args.data.wake.shard_id = 1;
    first_args.tag = 1;

    TaggedCompletionArgs second_args { };
    second_args.complete = &RecordSecondaryCompletion;
    second_args.owner = &trace;
    second_args.type = Network::CompletionArgs::Type::Wake;
    second_args.data.wake.shard_id = 1;
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
    conn.endpoint_id = 9;
    conn.shard_id = 3;
    conn.slot = 7;
    conn.generation.store(5, std::memory_order_release);

    const Network::ConnectionHandle conn_handle = conn.CreateHandle();
    const Network::ConnectionHandle same_conn_handle = conn.CreateHandle();
    assert(conn_handle.IsValid() && "[NetworkTests] Connection handle should remain valid for active transport peers.");
    assert(conn_handle == same_conn_handle && "[NetworkTests] Connection handles should preserve peer identity for equality checks.");
    assert(!(Network::ConnectionHandle { }) && "[NetworkTests] Default connection handles should be invalid.");

    const Network::ConnectionEvent evt { Network::ConnectionEventType::Connected, conn_handle };
    assert(evt.handle == conn_handle && "[NetworkTests] Connection events should continue to carry the transport peer handle.");
}

void SharedConnectionPoolPasses()
{
    Network::IOUringConfig io_uring_cfg { };
    io_uring_cfg.sq_entries = 64;
    io_uring_cfg.cq_entries = 64;
    io_uring_cfg.flags = 0;

    Network::NetworkShard shard(io_uring_cfg, 0, 0, 0, 2, 2);
    Status<void> status = shard.Initialize(io_uring_cfg, 1);
    assert(!status.IsFailed() && "[NetworkTests] NetworkShard should initialize its shared connection pool.");

    Network::Connection* first = shard.AcquireConnection(3, 2);
    Network::Connection* second = shard.AcquireConnection(4, 2);
    assert(first != nullptr && second != nullptr && first != second && "[NetworkTests] Shared connection pool should issue distinct slots.");
    assert(first->endpoint_id == 3 && second->endpoint_id == 4 && "[NetworkTests] Shared slots should preserve endpoint ownership.");
    assert(first->shard_id == 1 && second->shard_id == 1 && "[NetworkTests] Shared slots should use shard-global IDs.");

    assert(shard.AcquireConnection(5, 2) == nullptr && "[NetworkTests] Shared connection pool should report exhaustion.");

    first->state.store(Network::ConnectionState::Closing, std::memory_order_release);
    assert(shard.ReleaseConnection(*first) && "[NetworkTests] Closing connections should return to the shared free list.");
    Network::Connection* reused = shard.AcquireConnection(5, 2);
    assert(reused == first && reused->endpoint_id == 5 && "[NetworkTests] Released shared slots should be reusable by another endpoint.");
}

void SendBufferLayoutPasses()
{
    static_assert(Network::MAX_PACKET_SIZE == 1476, "[NetworkTests] Send payload capacity should stay aligned to the fixed MTU.");
    static_assert(Network::MAX_BUFFER_SIZE == 2048, "[NetworkTests] Send buffer capacity should stay fixed at 2048 bytes.");

    constexpr size_t metadata_offset = Network::BufferMetadata::SEND_OFFSET;
    constexpr size_t metadata_size = sizeof(Network::BufferMetadata);

    assert(metadata_offset >= Network::MAX_PACKET_SIZE && "[NetworkTests] Send metadata must begin after the payload region.");
    assert(metadata_offset + metadata_size <= Network::MAX_BUFFER_SIZE && "[NetworkTests] Send metadata must fit in the reserved tail space.");

    std::array<byte, Network::MAX_BUFFER_SIZE> buffer { };
    byte* const metadata_ptr = buffer.data() + metadata_offset;
    assert(metadata_ptr >= buffer.data() + Network::MAX_PACKET_SIZE && "[NetworkTests] Metadata pointer should remain outside the payload bytes.");
    assert(metadata_ptr + metadata_size <= buffer.data() + buffer.size() && "[NetworkTests] Metadata pointer should remain inside the backing buffer.");
}

void PendingBufferRingPasses()
{
    Network::PendingBufferRing ring { };
    byte* storage[2] = { nullptr, nullptr };
    byte first[4] = { };
    byte second[4] = { };
    byte third[4] = { };

    ring.Bind(storage, 2);
    assert(ring.IsEmpty() && "[NetworkTests] Pending buffer ring should start empty.");
    assert(!ring.IsFull() && "[NetworkTests] Pending buffer ring should not start full.");

    assert(ring.Push(first) && "[NetworkTests] Pending buffer ring should accept the first buffer.");
    assert(ring.Push(second) && "[NetworkTests] Pending buffer ring should accept the second buffer up to capacity.");
    assert(ring.IsFull() && "[NetworkTests] Pending buffer ring should report full once capacity is reached.");
    assert(!ring.Push(nullptr) && "[NetworkTests] Pending buffer ring should reject null buffers.");
    assert(!ring.Push(third) && "[NetworkTests] Pending buffer ring should reject pushes beyond capacity.");

    byte* popped = nullptr;
    assert(ring.Pop(popped) && popped == first && "[NetworkTests] Pending buffer ring should pop buffers in FIFO order.");
    assert(ring.Pop(popped) && popped == second && "[NetworkTests] Pending buffer ring should preserve the second buffered pointer.");
    assert(!ring.Pop(popped) && "[NetworkTests] Pending buffer ring should report empty after draining.");
    assert(ring.IsEmpty() && "[NetworkTests] Pending buffer ring should be empty after all buffered pointers are popped.");
}

void EndpointOwnerThreadContractPasses()
{
    OwnerThreadProbeEndpoint endpoint { };
    assert(endpoint.ProbeOwnerThread() && "[NetworkTests] Endpoint should claim the first caller as its owner thread.");

    bool second_thread_result = true;
    std::thread second_thread([&endpoint, &second_thread_result]()
    {
        second_thread_result = endpoint.ProbeOwnerThread();
    });
    second_thread.join();

    assert(!second_thread_result && "[NetworkTests] Endpoint owner-thread contract should reject calls from a second thread.");
}

Network::NetworkConfig MakeNetworkConfig(std::initializer_list<std::initializer_list<uint16>> server_shard_bindings)
{
    Network::NetworkConfig cfg { };
    cfg.shard_count = 2;
    cfg.io_uring.sq_entries = 64;
    cfg.io_uring.cq_entries = 64;
    cfg.io_uring.flags = 0;

    Network::IOBufferConfig io_buffer_cfg { };
    io_buffer_cfg.id = 0;
    io_buffer_cfg.nentries = 8;
    io_buffer_cfg.buffer_size = Network::MAX_BUFFER_SIZE;
    io_buffer_cfg.mmap_flags = 0;
    cfg.io_buffers.push_back(io_buffer_cfg);

    uint8 server_id = 0;
    for (const std::initializer_list<uint16>& shard_ids : server_shard_bindings)
    {
        Network::ServerConfig server_cfg { };
        server_cfg.id = server_id++;
        server_cfg.protocol = Network::TransportProtocol::Tcp;
        server_cfg.shard_ids.assign(shard_ids.begin(), shard_ids.end());
        server_cfg.bind_ip = in6addr_any;
        server_cfg.bind_port = static_cast<uint16>(40000 + server_cfg.id);
        server_cfg.io_buf_gid = 0;
        server_cfg.max_conns = 8;
        cfg.servers.push_back(server_cfg);
    }

    return cfg;
}

void MissingShardBindingsFail()
{
    Network::NetworkConfig cfg = MakeNetworkConfig({ { } });
    Network::NetworkManager manager { };
    Status<void> status = manager.Configure(cfg);
    assert(status.IsFailed() && "[NetworkTests] Configure should reject endpoints without shard bindings.");
}

void DuplicateShardBindingsFail()
{
    Network::NetworkConfig cfg = MakeNetworkConfig({ { 0, 0 } });
    Network::NetworkManager manager { };
    Status<void> status = manager.Configure(cfg);
    assert(status.IsFailed() && "[NetworkTests] Configure should reject duplicate shard bindings.");
}

void OutOfRangeShardBindingsFail()
{
    Network::NetworkConfig cfg = MakeNetworkConfig({ { 2 } });
    Network::NetworkManager manager { };
    Status<void> status = manager.Configure(cfg);
    assert(status.IsFailed() && "[NetworkTests] Configure should reject out-of-range shard bindings.");
}

void OverlappingShardBindingsPass()
{
    Network::NetworkConfig cfg = MakeNetworkConfig({ { 0 }, { 0, 1 } });
    Network::NetworkManager manager { };
    Status<void> status = manager.Configure(cfg);
    assert(!status.IsFailed() && "[NetworkTests] Configure should allow endpoints to overlap on the same shard.");
    assert(manager.GetServer(0) != nullptr && manager.GetServer(1) != nullptr && "[NetworkTests] Overlapping shard bindings should still create all configured servers.");
    manager.Release();
}

} // namespace

void NetworkPasses()
{
    TokenizedCompletionDispatchPasses();
    ConnectionHandlePasses();
    SharedConnectionPoolPasses();
    SendBufferLayoutPasses();
    PendingBufferRingPasses();
    EndpointOwnerThreadContractPasses();
    MissingShardBindingsFail();
    DuplicateShardBindingsFail();
    OutOfRangeShardBindingsFail();
    OverlappingShardBindingsPass();
}

} // namespace Tests
} // namespace Hypnos
} // namespace Blanketmen
