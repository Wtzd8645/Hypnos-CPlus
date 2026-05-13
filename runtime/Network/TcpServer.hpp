#pragma once

#include "Connection.hpp"
#include "Endpoint.hpp"
#include "NetworkShard.hpp"
#include "ServerShard.hpp"
#include "Hypnos/Network/NetworkConfig.hpp"
#include "Hypnos/Network/Server.hpp"

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class TcpServer : public Server, public Endpoint
{
public:
    using Server::Send;

    TcpServer(uint32 id, const ServerConfig& cfg, uint16 total_shard_count, const List<NetworkShard*>& bound_shards);
    ~TcpServer();

    Status<void> Start() override;
    Status<void> Stop() override;

    void Dispatch() override;
    void HandleShardWake(uint16 shard_id) override;
    Server* AsServer() noexcept override { return this; }

    Status<void> Close(ConnectionHandle conn_handle) override;
    Status<void> Send(ConnectionHandle conn_handle, const EncodedMessage& encoded) override;

private:
    const ServerConfig cfg;
    List<ServerShard*> server_shards;
    List<ServerShard*> server_shard_lookup;

    IPacketCodec* packet_codec = nullptr;
    IMessageCodec* message_codec = nullptr;

    ServerShard& GetServerShard(uint16 shard_id) const;
    Connection* ResolveHandle(ConnectionHandle conn_handle, ServerShard*& server_shard) const;
    bool IsBoundConnection(const ServerShard& server_shard, const Connection& conn) const noexcept;

    void DrainReturnedBuffers();
    bool TryReserveQueuedSend(Connection& conn) const;
    void ReleaseQueuedSend(Connection& conn) noexcept;
    void ReleaseBufferReference(NetworkShard& shard, byte* buffer) noexcept;
    void FinalizeClosingConnection(ServerShard& server_shard, Connection& conn);
    void CloseInternal(ServerShard& server_shard, Connection& conn);
    void DrainIngressCommands(ServerShard& server_shard);
    void ExecuteClose(ServerShard& server_shard, Connection* conn, uint32 generation);
    void ExecuteSend(ServerShard& server_shard, Connection* conn, uint32 generation, byte* buffer);
    void AcceptInternal(ServerShard& server_shard, CompletionArgs* args);
    void ReceiveInternal(ServerShard& server_shard, CompletionArgs* args);
    void SendInternal(ServerShard& server_shard, CompletionArgs* args, const void* buf, int32 len);

    bool IsRecvStale(const CompletionArgs& args) const noexcept;
    bool IsSendStale(const CompletionArgs& args) const noexcept;
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
