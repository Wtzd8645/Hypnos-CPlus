#include "Hypnos/Logging.hpp"
#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <Hypnos-Core/Memory/Memory.hpp>
#include <Hypnos-Core/Threads.hpp>
#include <cstddef>
#include <liburing.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {

TcpServer::TcpServer() :
    io_recv_buf_pool(2048, MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB, 8192),
    io_send_buf_pool(2048, MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB, 8192),
    events(1024), request_factory(nullptr), requests(8192), responses(8192)
{
    memset(&io_params, 0, sizeof(io_params));
    io_params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN; // TODO: Make configurable.

    buf_meta_offset = Memory::AlignUp(2048, alignof(BufferMetadata));
    send_evt = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

TcpServer::~TcpServer()
{
    Release();
    delete request_factory;
}

void TcpServer::Initialize()
{
    if (sock >= 0)
    {
        Logging::Error("[TcpSocket] Socket is already initialized.");
        return;
    }

    try
    {
        sock = socket(AF_INET6, SOCK_STREAM, 0);
        if (sock < 0)
        {
            throw std::runtime_error("[TcpSocket] Failed to create socket. Error: " + std::string(strerror(errno)));
        }

        int opt = 0;
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
        {
            throw std::runtime_error("[TcpSocket] Failed to set socket options.");
        }

        //setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)); // TODO: Make configurable.
        //setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)); // TODO: Make configurable.

        sockaddr_in6 sock_addr = { AF_INET6, htons(27015), 0, in6addr_any, 0 };
        if (bind(sock, (sockaddr*)&sock_addr, sizeof(sock_addr)) < 0)
        {
            Logging::Error("[TcpSocket] Failed to bind socket.");
            return;
        }

        // Initialize io_uring. // TODO: Make configurable.
        uint32 buf_size = 2048;
        uint32 buf_num = 8192;
        if (io_uring_queue_init_params(512, &io_ring, &io_params) < 0)
        {
            throw std::runtime_error("[TcpSocket] Failed to initialize io_uring.");
        }

        // Setup recv buf ring.
        int err;
        io_recv_buf_ring = io_uring_setup_buf_ring(&io_ring, buf_num, IO_RECV_BUF_GROUP, 0, &err);
        if (io_recv_buf_ring == nullptr)
        {
            throw std::runtime_error("[TcpSocket] Failed to setup buf ring. Error: " + std::to_string(err));
        }

        // Allocate and add buffers to recv buf ring. 
        io_uring_buf_ring_init(io_recv_buf_ring);
        io_recv_buf_mask = io_uring_buf_ring_mask(buf_num);
        for (size_t i = 0; i < buf_num; ++i)
        {
            io_uring_buf_ring_add(io_recv_buf_ring, io_recv_buf_pool[i], buf_size, i, io_recv_buf_mask, i);
        }
        io_uring_buf_ring_advance(io_recv_buf_ring, buf_num);
    }
    catch (const std::exception& e)
    {
        Logging::Error("[TcpSocket] Failed to initialize socket. Error: %s", e.what());
        Release();
    }
}

void TcpServer::Release()
{
    if (running)
    {
        running = false;
        for (auto& pair : connection_map)
        {
            Close(pair.second);
        }

        if (io_thread->joinable())
        {
            io_thread->join();
        }
    }

    if (sock >= 0)
    {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        sock = -1;
        io_uring_queue_exit(&io_ring);
    }

    if (io_recv_buf_ring != nullptr)
    {
        io_uring_free_buf_ring(&io_ring, io_recv_buf_ring, 4096, 0);
        io_recv_buf_ring = nullptr;
    }
}

void TcpServer::Listen()
{
    if (sock < 0 || running)
    {
        Logging::Error("[TcpSocket] Socket is not initialized or already running.");
        return;
    }

    if (listen(sock, 256) < 0) // TODO: Make configurable.
    {
        Logging::Error("[TcpSocket] Failed to listen on socket.");
        return;
    }

    Logging::Info("[TcpSocket] Listening on socket.");
    running = true;
    Accept();

    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_prep_poll_multishot(sqe, send_evt, POLLIN); // io_uring_prep_read_multishot(sqe, send_evt, 0, 0, 0);
}

// NOTE: Main thread.
void TcpServer::Dispatch()
{
    ConnectionEvent evt;
    while (events.Dequeue(evt))
    {
        auto it = event_handlers.find(evt.sockId);
        if (it != event_handlers.end())
        {
            it->second->Handle(evt);
        }
    }

    int32 req_count = 0;
    uint8* buffer = nullptr;
    while (requests.Dequeue(buffer))
    {
        ReceiveMetadata& buf_meta = ReceiveMetadata::Get(buffer, buf_meta_offset);
        // TOOD: Unpack request.
        //EventDispatcher<uint16, RequestBase*>::Dispatch(req->header.msgId, req);
        // TODO: Recycle requests outside.
        io_uring_buf_ring_add(io_recv_buf_ring, buffer, 2048, buf_meta.bid, io_recv_buf_mask, req_count++);
    }

    if (req_count > 0)
    {
        io_uring_buf_ring_advance(io_recv_buf_ring, req_count);
    }
}

inline void TcpServer::Close(Connection* conn)
{
    connection_map.erase(conn->sock); // TODO: Optimize close operation.
    shutdown(conn->sock, SHUT_RDWR);
    close(conn->sock);
    // Notify event handlers.
}

inline void TcpServer::Accept()
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_sqe_set_data(sqe, new EventArg { SocketOp::ACPT }); // TODO: Make EventArg pool.
    io_uring_prep_multishot_accept(sqe, sock, (sockaddr*)&accept_conn.addr, &accept_conn.addr_len, 0);
}

inline void TcpServer::Receive(Connection* conn)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    io_uring_sqe_set_data(sqe, new EventArg { SocketOp::RECV, conn }); // TODO: Make EventArg pool.
    sqe->buf_group = 0;
    io_uring_prep_recv_multishot(sqe, conn->sock, nullptr, 0, 0); // TODO: Pass buffer size.
}

void TcpServer::Send(ResponseBase* resp)
{
    uint8* buf = io_send_buf_pool.Pop();
    resp->Pack(buf, 2048);
    SendMetadata::Get(buf, buf_meta_offset).ref_count = resp->conns.Size();
    responses.Enqueue(resp);

    if (!processing.load(std::memory_order_relaxed))
    {
        eventfd_write(send_evt, 1);
    }
}

inline void TcpServer::SendInternal(Connection* conn)
{
    PacketContext& ctx = conn->send_ctx;
    if (ctx.pending_bytes > 0)
    {
        SendInternal(conn, ctx.buffer + ctx.processed_bytes, ctx.pending_bytes);
        return;
    }

    if (--SendMetadata::Get(ctx.buffer, buf_meta_offset).ref_count == 0)
    {
        io_send_buf_pool.Push(ctx.buffer);
    }

    if (!ctx.next_buffers.empty())
    {
        ctx.buffer = ctx.next_buffers.front();
        ctx.pending_bytes = SendMetadata::Get(ctx.buffer, buf_meta_offset).size;
        ctx.processed_bytes = 0;
        ctx.next_buffers.pop();
        SendInternal(conn, ctx.buffer, ctx.pending_bytes);
    }
}

inline void TcpServer::SendInternal(Connection* conn, const void* buf, int32 len)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_sqe_set_data(sqe, new EventArg { SocketOp::SEND, conn }); // TODO: Make EventArg pool.
    io_uring_prep_send_zc(sqe, conn->sock, buf, len, 0, 0);
}

// NOTE: Network thread.
void TcpServer::ProcessEvents()
{
    try
    {
        int32 res = 0;
        io_uring_cqe* cqes;
        uint32 cq_head;
        io_uring_cqe* cqe;
        EventArg* arg;
        while (running)
        {
            res = io_uring_wait_cqe(&io_ring, &cqes);
            processing.store(true, std::memory_order_relaxed);
            if (res < 0)
            {
                OnCqeError(-res);
                continue;
            }

            int32 cqe_count = 0;
            io_uring_for_each_cqe(&io_ring, cq_head, cqe)
            {
                ++cqe_count;
                arg = static_cast<EventArg*>(io_uring_cqe_get_data(cqe));
                if (arg == nullptr)
                {
                    continue;
                }

                switch (arg->op)
                {
                    case SocketOp::ACPT: { OnAccept(cqe->res, cqe->flags); break; }
                    case SocketOp::RECV: { OnReceive(arg->conn, cqe->res, cqe->flags); break; }
                    case SocketOp::SEND: { OnSend(arg->conn, cqe->res, cqe->flags); break; }
                    default: { Logging::Error("[TcpSocket] Unknown operation type."); break; }
                }
            }
            io_uring_cq_advance(&io_ring, cqe_count);

            ResponseBase* resp = nullptr;
            while (responses.Dequeue(resp))
            {
                uint8* buf = resp->buffer;
                packet_size len = resp->length;
                for (Connection* conn : resp->conns)
                {
                    PacketContext& ctx = conn->send_ctx;
                    if (ctx.pending_bytes > 0)
                    {
                        ctx.next_buffers.push(buf);
                    }
                    else
                    {
                        ctx.buffer = buf;
                        ctx.pending_bytes = len;
                        ctx.processed_bytes = 0;
                        SendInternal(conn, buf, len);
                    }
                }
                // TODO: Recycle responses.
            }
            processing.store(false, std::memory_order_relaxed);
        }
    }
    catch (const std::exception& e)
    {
        Logging::Error("[TcpSocket] Failed to process events. Error: %s", e.what());
        Release();
    }
}

inline void TcpServer::OnCqeError(int err)
{
    static const std::unordered_set<int> retriable_errors = { EIO, EAGAIN, ENOMEM, EBUSY };
    static const std::unordered_set<int> fatal_errors = { ENXIO, EBADF, EFAULT, EINVAL, ENOSPC };

    if (retriable_errors.contains(err))
    {
        Logging::Warning("[TcpSocket] Failed to peek CQE. Error: %s", strerror(errno));
        usleep(32); // TODO: Make configurable.
        return;
    }

    if (fatal_errors.contains(err))
    {
        throw std::runtime_error("[TcpSocket] Fatal CQE error. Error: " + std::string(strerror(errno)));
    }

    throw std::runtime_error("[TcpSocket] Unknown CQE error. Error: " + std::string(strerror(errno)));
}

inline void TcpServer::OnAccept(int32 res, uint32 flags)
{
    static const std::unordered_set<int> retriable_errors = { EAGAIN, ECONNABORTED };
    static const std::unordered_set<int> resource_errors = { ENOMEM, ENFILE, EMFILE, ENOBUFS };
    static const std::unordered_set<int> fatal_errors = { EBADF, EFAULT, EINVAL, ENOTSOCK };

    if (res < 0)
    {
        int32 err = -res;
        if (resource_errors.contains(err))
        {
            Logging::Error("[TcpSocket] Resource limit reached while accepting connection.");
            usleep(32); // TODO: Make configurable.
        }
        else if (fatal_errors.contains(err))
        {
            throw std::runtime_error("[TcpSocket] Fatal accept error." + std::string(strerror(errno)));
        }
    }
    else
    {
        auto it = connection_map.find(res);
        Connection* conn = (it != connection_map.end()) ? it->second : nullptr;
        if (conn == nullptr)
        {
            conn = new Connection; // TODO: Use thread-safe object pool.
            connection_map[res] = conn;
        }

        conn->sock = res;
        conn->addr = accept_conn.addr;
        conn->addr_len = accept_conn.addr_len;
        Receive(conn);
    }

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        Accept();
    }
}

inline void TcpServer::OnReceive(Connection* conn, int32 res, uint32 flags)
{
    if (res <= 0)
    {
        Logging::Info("[TcpSocket] Connection closed. Socket: %d, Error: %s", conn->sock, strerror(-res));
        Close(conn);
        return;
    }
    
    Logging::Info("[TcpSocket] Received %d bytes.", res);
    int32 bid = flags & IORING_CQE_BUFFER_SHIFT;
    uint8* buf = io_recv_buf_pool[bid];
    ReceiveMetadata& buf_meta = ReceiveMetadata::Get(buf, buf_meta_offset);
    buf_meta.conn = conn;
    buf_meta.bid = bid;
    buf_meta.size = res;
    requests.Enqueue(buf);

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        Receive(conn);
    }
}

inline void TcpServer::OnSend(Connection* conn, int32 res, uint32 flags)
{
    if ((flags & IORING_CQE_F_NOTIF) != 0)
    {
        SendInternal(conn);
        return;
    }

    if (res <= 0)
    {
        Logging::Info("[TcpSocket] Connection closed while sending. Socket: %d, Error: %s)", conn->sock, strerror(-res));
        Close(conn);
        return;
    }

    Logging::Info("[TcpSocket] Sent %d bytes.", res);
    PacketContext& ctx = conn->send_ctx;
    ctx.pending_bytes -= res;
    ctx.processed_bytes += res;

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        SendInternal(conn);
    }
}

} // namespace Hypnos
} // namespace Blanketmen