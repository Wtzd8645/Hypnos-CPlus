#pragma once

#include "Connection.hpp"
#include "EndpointRuntime.hpp"
#include "NetworkShard.hpp"
#include "Hypnos/Network/NetworkConfig.hpp"
#include "Hypnos/Network/Server.hpp"
#include <Hypnos-Core/Container/SpscRingBuffer.hpp>
#include <span>
#include <sys/socket.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

enum class ServerShardCommandType : uint8
{
    CloseBatch,
    SendBatch
};

struct ServerShardCommand
{
    ServerShardCommandType type = ServerShardCommandType::SendBatch;
    ConnectionHandle* conn_handles = nullptr;
    uint32 conn_handle_count = 0;
    byte* buffer = nullptr;
};

struct ServerShard
{
    NetworkShard& shard;
    int32 listen_fd = INVALID_FD;
    sockaddr_storage accept_addr { };
    socklen_t accept_addr_len = sizeof(sockaddr_storage);

    SpscRingBuffer<ConnectionEvent> egress_connection_events;
    SpscRingBuffer<IMessage*> egress_requests;
    SpscRingBuffer<ServerShardCommand> ingress_commands;

    ServerShard(NetworkShard& shard, size_t queue_capacity_hint, uint32 max_pending_buffers);
};

class TcpServer : public Server, public EndpointRuntime
{
public:
    TcpServer(uint32 id, const ServerConfig& cfg, uint16 total_shard_count, const List<NetworkShard*>& bound_shards);
    ~TcpServer();

    Status<void> Start() override;
    Status<void> Stop() override;

    void Dispatch() override;
    void OnShardWake(uint16 shard_id) override;
    Server* AsServer() noexcept override { return this; }

    Status<void> Close(std::span<const ConnectionHandle> conn_handles) override;
    Status<void> Broadcast(const IMessage& message) override;
    Status<void> Send(std::span<const ConnectionHandle> conn_handles, const IMessage& message) override;

private:
    struct AdmissionRecord
    {
        Connection* conn = nullptr;
        uint16 shard_id = 0;
    };

    struct PreparedCommand
    {
        ServerShard* server_shard = nullptr;
        ConnectionHandle* conn_handles = nullptr;
        uint32 conn_handle_count = 0;
        byte* buffer = nullptr;
    };

    const ServerConfig cfg;
    List<ServerShard*> server_shards;
    List<ServerShard*> server_shard_lookup;
    List<uint32> shard_target_counts;
    List<uint32> shard_target_offsets;
    List<uint16> touched_shards;
    List<AdmissionRecord> admission_records;
    List<PreparedCommand> prepared_commands;

    IPacketCodec* packet_codec = nullptr;
    IMessageCodec* message_codec = nullptr;

    ServerShard& GetServerShard(uint16 shard_id) const;
    Connection* ResolveHandle(ServerShard& server_shard, const ConnectionHandle& handle) const;

    void DrainReturnedBuffers();
    bool TryReserveQueuedSend(Connection& conn) const;
    void ReleaseQueuedSend(Connection& conn) noexcept;
    void ReleaseBufferReference(NetworkShard& shard, byte* buffer) noexcept;
    void FinalizeClosingConnection(ServerShard& server_shard, Connection& conn);
    void CloseInternal(ServerShard& server_shard, Connection& conn);
    void DrainIngressCommands(ServerShard& server_shard);
    void ExecuteCloseBatch(ServerShard& server_shard, ConnectionHandle* conn_handles, uint32 conn_handle_count);
    void ExecuteSendBatch(ServerShard& server_shard, ConnectionHandle* conn_handles, uint32 conn_handle_count, byte* buffer);
    void AcceptInternal(ServerShard& server_shard, CompletionArgs* args);
    void ReceiveInternal(ServerShard& server_shard, CompletionArgs* args);
    void SendInternal(ServerShard& server_shard, CompletionArgs* args, const void* buf, int32 len);

    bool IsStale(const CompletionArgs& args) const noexcept;
    void ReleaseReceiveArgs(ServerShard& server_shard, CompletionArgs* args, uint32 flags);
    void ReleaseSendArgs(ServerShard& server_shard, CompletionArgs* args, uint32 flags);
    bool HandlePacket(ServerShard& server_shard, const TransportHeader& header, byte* src);

    static void OnAcceptCqe(int32 res, uint32 flags, CompletionArgs* args);
    static void OnReceiveCqe(int32 res, uint32 flags, CompletionArgs* args);
    static void OnSendCqe(int32 res, uint32 flags, CompletionArgs* args);

    void OnAccept(ServerShard& server_shard, int32 res, uint32 flags, CompletionArgs& args);
    void OnReceive(int32 res, uint32 flags, CompletionArgs& args);
    void OnSend(int32 res, uint32 flags, CompletionArgs& args);
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
