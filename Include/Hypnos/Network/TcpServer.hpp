#pragma once

#include "Connection.hpp"
#include "IOContext.hpp"
#include "NetworkConfig.hpp"
#include "NetworkDefs.hpp"
#include "ServerBase.hpp"
#include <Hypnos-Core/Cache/BufferPool.hpp>
#include <Hypnos-Core/Cache/ObjectPool.hpp>
#include <Hypnos-Core/Cache/SparseObjectPool.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class TcpServer : public ServerBase
{
public:
    TcpServer(uint32 id, const ServerConfig& cfg, const List<IOContext*>& ctxs);
    ~TcpServer();

    Status<void> Start() override;
    Status<void> Stop() override;

    void Dispatch() override;
    void OnReactorWake(uint16 rid) override;

    Status<void> Close(const List<ConnectionHandle>& conn_handles) override;
    Status<void> Broadcast(IMessage* resp) override;
    Status<void> Send(const List<ConnectionHandle>& conn_handles, IMessage* resp) override;

private:
    struct ShardState
    {
        TcpServer& server;
        uint16 rid;
        IOContext& io_ctx;
        int32 listen_fd = INVALID_FD;
        sockaddr_storage accept_addr { };
        socklen_t accept_addr_len = sizeof(sockaddr_storage);
        HeapObjectPool<CompletionArgs> args_pool;
        BufferPool<MmapAllocatePolicy<MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB>> framing_buffer_pool;
        SparseObjectPool<Connection> connections;
        SpscRingBuffer<ConnectionEvent> conn_events;
        SpscRingBuffer<IMessage*> requests;
        SpscRingBuffer<ResponseArgs> command_queue;

        ShardState(TcpServer& server, uint16 rid, IOContext& io_ctx, size_t capacity);
    };

    const ServerConfig cfg;
    const List<IOContext*>& io_contexts;
    List<ShardState*> shards;

    IPacketCodec* packet_codec = nullptr;
    IMessageCodec* message_codec = nullptr;

    ShardState& GetShard(uint16 rid) const;
    Connection* ResolveHandle(ShardState& shard, const ConnectionHandle& handle) const;

    void CloseInternal(ShardState& shard, Connection* conn);
    Status<void> EnqueueCommand(ShardState& shard, ResponseArgs&& args);
    void DrainCommands(ShardState& shard);
    void AcceptInternal(ShardState& shard, CompletionArgs* args);
    void ReceiveInternal(ShardState& shard, CompletionArgs* args);
    void SendInternal(ShardState& shard, CompletionArgs* args, const void* buf, int32 len);

    void BindAcceptArgs(ShardState& shard, CompletionArgs& args) const;
    void BindReceiveArgs(ShardState& shard, CompletionArgs& args, Connection& conn) const;
    void BindSendArgs(ShardState& shard, CompletionArgs& args, Connection& conn) const;
    bool IsStale(const CompletionArgs& args) const noexcept;
    void ReleaseReceiveArgs(ShardState& shard, CompletionArgs* args, uint32 flags);
    void ReleaseSendArgs(ShardState& shard, CompletionArgs* args, uint32 flags);
    bool HandlePacket(ShardState& shard, const TransportHeader& header, byte* src);

    static void OnAcceptCqe(int32 res, uint32 flags, CompletionArgs* args);
    static void OnReceiveCqe(int32 res, uint32 flags, CompletionArgs* args);
    static void OnSendCqe(int32 res, uint32 flags, CompletionArgs* args);

    void OnAccept(ShardState& shard, int32 res, uint32 flags, CompletionArgs& args);
    void OnReceive(ShardState& shard, int32 res, uint32 flags, CompletionArgs& args);
    void OnSend(ShardState& shard, int32 res, uint32 flags, CompletionArgs& args);
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
