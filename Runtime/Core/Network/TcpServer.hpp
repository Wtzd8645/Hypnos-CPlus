#pragma once

#include "Connection.hpp"
#include "IOContext.hpp"
#include "NetworkConfig.hpp"
#include "NetworkDefs.hpp"
#include "RequestAllocatorBase.hpp"
#include "ResponseAllocatorBase.hpp"
#include "ServerSocketBase.hpp"
#include <Hypnos-Kernel/Cache/ObjectPool.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class TcpServer : public ServerSocketBase
{
public:
    TcpServer(SocketConfig& cfg, IOContext& ctx);
    ~TcpServer();

    void Start() override;
    void Stop() override;

    void Dispatch() override;
    void ProcessIOEvent(IOEventArgs* args, int32 res, uint32 flags) override;

    void Close(List<ConnectionHandle>* conn_handles) override;
    void Send(List<ConnectionHandle>* conn_handles, ResponseBase* resp) override;

private:
    static constexpr int32 IO_RECV_BUF_GROUP = 0;

    alignas(CACHE_LINE_SIZE) Atomic<uint64> poll_head { 0 };
    alignas(CACHE_LINE_SIZE) Atomic<uint64> poll_tail { 0 };
    ObjectPool<IOEventArgs> event_args_pool;

    void CloseInternal(Connection* conn);
    void PollInternal(IOEventArgs* args);
    void AcceptInternal(IOEventArgs* args);
    void ReceiveInternal(IOEventArgs* args);
    void SendInternal(IOEventArgs* args, const void* buf, int32 len);

    void OnPoll(IOEventArgs* args, int32 res, uint32 flags);
    void OnAccept(IOEventArgs* args, int32 res, uint32 flags);
    void OnReceive(IOEventArgs* args, int32 res, uint32 flags);
    void OnSend(IOEventArgs* args, int32 res, uint32 flags);
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen