#pragma once

#include "Connection.hpp"
#include "IOContext.hpp"
#include "NetworkConfig.hpp"
#include "NetworkDefs.hpp"
#include "ServerBase.hpp"
#include <Hypnos-Core/Cache/BufferPool.hpp>
#include <Hypnos-Core/Cache/ObjectPool.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class TcpServer : public ServerBase
{
public:
    TcpServer(uint32 id, const ServerConfig& cfg, IOContext& ctx);
    ~TcpServer();

    Status<void> Start() override;
    Status<void> Stop() override;

    void Process(const IOEvent& evt) override;
    void Dispatch() override;

    void Close(List<ConnectionHandle>* conn_handles) override;
    void Broadcast(IMessage* resp) override;
    void Send(List<ConnectionHandle>* conn_handles, IMessage* resp) override;

private:
    alignas(CACHE_LINE_SIZE) Atomic<uint64> poll_count { 0 };

    const ServerConfig cfg;
    IOContext& io_ctx;
    HeapObjectPool<IOEventArgs> event_args_pool;
    BufferPool<MmapAllocatePolicy<MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB>> framing_buffer_pool;

    IPacketCodec* packet_codec = nullptr;
    IMessageCodec* message_codec = nullptr;
    SpscRingBuffer<IMessage*> requests;

    SpscRingBuffer<ResponseArgs> response_args;

    void CloseInternal(Connection* conn);
    void AcceptInternal(IOEventArgs* args);
    void ReceiveInternal(IOEventArgs* args);
    void PollInternal(IOEventArgs* args);
    void SendInternal(IOEventArgs* args, const void* buf, int32 len);

    void ReleaseEventArgs(IOEventArgs* args, uint32 flags);
    bool HandlePacket(const TransportHeader& header, byte* src);

    void OnAccept(const IOEvent& evt);
    void OnReceive(const IOEvent& evt);
    void OnPoll(const IOEvent& evt);
    void OnSend(const IOEvent& evt);
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
