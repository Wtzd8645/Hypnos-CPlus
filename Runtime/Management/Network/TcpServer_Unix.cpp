#include "Hypnos/Logging.hpp"
#include "Hypnos/Network/NetworkDefinition.hpp"
#include "Hypnos/Network/TcpServer.hpp"
#include <Hypnos-Core/Memory/Memory.hpp>
#include <cstring>
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
    io_recv_buf_pool(MAX_BUFFER_SIZE, MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB, 8192),
    io_send_buf_pool(MAX_BUFFER_SIZE, MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB, 8192),
    conn_events(1024), request_pool(nullptr), requests(8192), socket_op_args(8192)
{
    std::memset(&io_params, 0, sizeof(io_params));
    io_params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN; // TODO: Make configurable.
    io_params.sq_thread_idle = 8000;

    buf_meta_offset = Memory::AlignUp(MAX_PACKET_SIZE, alignof(buffer_metadata));
    send_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

TcpServer::~TcpServer()
{
    Release();
    delete request_pool;
}

void TcpServer::Initialize()
{
    if (sock > INVALID_FD)
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

        // Initialize io_uring.
        uint32 buf_num = 8192; // TODO: Make configurable.
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
        io_recv_buf_mask = io_uring_buf_ring_mask(buf_num);
        for (int32 i = 0; i < buf_num; ++i)
        {
            io_uring_buf_ring_add(io_recv_buf_ring, io_recv_buf_pool[i], MAX_BUFFER_SIZE, i, io_recv_buf_mask, i);
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
    if (running.load(std::memory_order_relaxed))
    {
        running.store(false, std::memory_order_relaxed);
        if (io_thread.joinable())
        {
            io_thread.join();
        }
    }

    // TODO: Close all connections.
    /*for (auto& conn_pair : connection_map)
    {
        Close(conn_pair.second);
    }*/

    if (sock > INVALID_FD)
    {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        sock = INVALID_FD;
    }

    if (io_recv_buf_ring != nullptr)
    {
        io_uring_queue_exit(&io_ring);
        io_uring_free_buf_ring(&io_ring, io_recv_buf_ring, 4096, 0);
        io_recv_buf_ring = nullptr;
    }
}

void TcpServer::Listen()
{
    if (sock <= INVALID_FD || running.load(std::memory_order_relaxed))
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
    socket_event_args* acpt_args = socket_event_args_pool.Pop();
    acpt_args->op = socket_operation::ACPT;
    AcceptInternal(acpt_args);

    socket_event_args* poll_args = socket_event_args_pool.Pop();
    poll_args->op = socket_operation::POLL;
    PollInternal(poll_args);

    io_thread = Thread(&TcpServer::ProcessEvents, this);
}

void TcpServer::Dispatch()
{
    ConnectionEvent evt;
    while (conn_events.Dequeue(evt))
    {
        auto it = conn_event_handlers.find(evt.evtId);
        if (it != conn_event_handlers.end())
        {
            it->second->Handle(evt.conn);
        }
    }

    RequestBase* req = nullptr;
    while (requests.Dequeue(req))
    {
        EventHandlerBase<RequestBase*>*& handler = request_handlers[req->gid];
        if (handler != nullptr)
        {
            handler->Handle(req);
        }
        request_pool->Release(req);
    }

    // PERF: Maybe change back to submit and wait.
    if (io_ring.sq.kflags[0] & IORING_SQ_NEED_WAKEUP)
    {
        io_uring_enter(io_ring.ring_fd, 0, 0, IORING_ENTER_SQ_WAKEUP, NULL);
    }
}

void TcpServer::Close(Container::List<ConnectionHandle>* conn_handles)
{
    SocketOperationArgs args = { conn_handles, nullptr };
    while (!socket_op_args.Enqueue(args))
    {
        std::this_thread::yield();
    }
    
    if (polling.fetch_add(1, std::memory_order_release) == 0)
    {
        eventfd_write(send_efd, 1);
    }
}

void TcpServer::Send(Container::List<ConnectionHandle>* conn_handles, ResponseBase* resp)
{
    SocketOperationArgs args = { conn_handles, io_send_buf_pool.Pop() };
    resp->Pack(args);
    response_pool->Release(resp);
    while (!socket_op_args.Enqueue(args))
    {
        std::this_thread::yield();
    }

    if (polling.fetch_add(1, std::memory_order_release) == 0)
    {
        eventfd_write(send_efd, 1);
    }
}

void TcpServer::ProcessEvents()
{
    int32 res;
    io_uring_cqe* cqes;
    uint32 cq_head;
    io_uring_cqe* cqe;
    running.store(true, std::memory_order_relaxed);
    while (running)
    {
        res = io_uring_wait_cqe(&io_ring, &cqes);
        if (res < 0)
        {
            OnCqeError(-res);
            continue;
        }

        io_uring_for_each_cqe(&io_ring, cq_head, cqe)
        {
            socket_event_args* args = static_cast<socket_event_args*>(io_uring_cqe_get_data(cqe));
            switch (args->op)
            {
                case socket_operation::ACPT: { OnAccept(args, cqe->res, cqe->flags); break; }
                case socket_operation::RECV: { OnReceive(args, cqe->res, cqe->flags); break; }
                case socket_operation::POLL: { OnPoll(args, cqe->res, cqe->flags); break; }
                case socket_operation::SEND: { OnSend(args, cqe->res, cqe->flags); break; }
                default: { Logging::Error("[TcpSocket] Unknown operation type."); break; }
            }
        }

        if (buf_count > 0)
        {
            io_uring_buf_ring_advance(io_recv_buf_ring, buf_count);
            buf_count = 0;
        }
        io_uring_cq_advance(&io_ring, cq_head - io_ring.cq.khead[0]);
    }
}

void TcpServer::CloseInternal(Connection* conn)
{
    if (conn->sock <= INVALID_FD)
    {
        return;
    }

    shutdown(conn->sock, SHUT_RDWR);
    close(conn->sock);
    conn->sock = INVALID_FD;
    conn->version++;

    recv_context& recv_ctx = conn->recv_ctx;
    recv_ctx.packet_bytes = 0;
    recv_ctx.waiting_bytes = sizeof(packet_size);
    recv_ctx.received_bytes = 0;

    send_context& send_ctx = conn->send_ctx;
    send_ctx.pending_bytes = 0;
    send_ctx.processed_bytes = 0;

    if (send_ctx.buffer != nullptr)
    {
        auto& send_meta = buffer_metadata::get(send_ctx.buffer, buf_meta_offset).send;
        if (--send_meta.conn_count == 0)
        {
            io_send_buf_pool.Push(send_ctx.buffer);
        }
        send_ctx.buffer = nullptr;
    }

    while (!send_ctx.pending_responses.empty())
    {
        uint8* buf = send_ctx.pending_responses.front();
        auto& send_meta = buffer_metadata::get(buf, buf_meta_offset).send;
        if (--send_meta.conn_count <= 0)
        {
            io_send_buf_pool.Push(buf);
        }
        send_ctx.pending_responses.pop();
    }

    connection_pool.Push(conn);
}

inline void TcpServer::AcceptInternal(socket_event_args* args)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_multishot_accept(sqe, sock, (sockaddr*)&acpt_addr, &acpt_addr_len, 0);
}

inline void TcpServer::ReceiveInternal(socket_event_args* args)
{
    args->version = args->conn->version;
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    sqe->buf_group = IO_RECV_BUF_GROUP;
    io_uring_prep_recv_multishot(sqe, args->conn->sock, nullptr, 0, 0);
}

inline void TcpServer::PollInternal(socket_event_args* args)
{
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_poll_multishot(sqe, send_efd, POLLIN);
}

inline void TcpServer::SendInternal(socket_event_args* args, const void* buf, int32 len)
{
    // TODO: When packet exceeds threshold, use io_uring_prep_send_zc?
    args->version = args->conn->version;
    io_uring_sqe* sqe = io_uring_get_sqe(&io_ring);
    io_uring_sqe_set_data(sqe, args);
    io_uring_prep_send(sqe, args->conn->sock, buf, len, 0);
}

inline void TcpServer::OnCqeError(int32 err)
{
    static const std::unordered_set<int> retriable_errors = { EIO, EAGAIN, ENOMEM, EBUSY };

    if (retriable_errors.contains(err))
    {
        Logging::Warning("[TcpSocket] Failed to wait for CQE. Error: %s", strerror(err));
        usleep(8); // TODO: Make configurable.
        return;
    }

    Logging::Error("[TcpSocket] Failed to wait for CQE.");
    running.store(false, std::memory_order_relaxed);
    shutdown(sock, SHUT_RDWR);
    close(sock);
    sock = INVALID_FD;
    // TODO: Notify event handlers.
}

inline void TcpServer::OnAccept(socket_event_args* args, int32 res, uint32 flags)
{
    static const std::unordered_set<int> fatal_errors =
    {
        EPERM, EBADF, EFAULT, EINVAL, ENOTSOCK, EOPNOTSUPP,
        EAFNOSUPPORT, EADDRINUSE, EADDRNOTAVAIL,
        ENETDOWN, ESHUTDOWN
    };

    if (res >= 0)
    {
        socket_event_args* recv_args = socket_event_args_pool.Pop();
        recv_args->op = socket_operation::RECV;
        recv_args->conn = connection_pool.Pop();
        recv_args->conn->sock = res;
        ReceiveInternal(recv_args);
    }

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        if (res >= 0)
        {
            AcceptInternal(args);
            return;
        }

        if (fatal_errors.contains(-res))
        {
            Logging::Error("[TcpSocket] Failed to accept connection. Error: %s", strerror(-res));
            socket_event_args_pool.Push(args);
            return;
        }

        Logging::Warning("[TcpSocket] Failed to accept connection. Error: %s", strerror(-res));
        std::this_thread::yield();
        AcceptInternal(args);
    }
}

inline void TcpServer::OnReceive(socket_event_args* args, int32 res, uint32 flags)
{
    if (args->version != args->conn->version)
    {
        if ((flags & IORING_CQE_F_BUFFER) != 0)
        {
            int32 bid = flags >> IORING_CQE_BUFFER_SHIFT;
            uint8* buf = io_recv_buf_pool[bid];
            io_uring_buf_ring_add(io_recv_buf_ring, buf, MAX_BUFFER_SIZE, bid, io_recv_buf_mask, buf_count++);
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            socket_event_args_pool.Push(args);
        }
    }
    else if (res <= 0)
    {
        Logging::Info("[TcpSocket] Connection closed. Socket: %d, Error: %s", args->conn->sock, strerror(-res));
        CloseInternal(args->conn);

        if ((flags & IORING_CQE_F_BUFFER) != 0)
        {
            int32 bid = flags >> IORING_CQE_BUFFER_SHIFT;
            uint8* buf = io_recv_buf_pool[bid];
            io_uring_buf_ring_add(io_recv_buf_ring, buf, MAX_BUFFER_SIZE, bid, io_recv_buf_mask, buf_count++);
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            socket_event_args_pool.Push(args);
        }
    }
    else
    {
        packet_size received_bytes = res;
        packet_size processed_bytes = 0;
        if ((flags & IORING_CQE_F_BUFFER) != 0)
        {
            Logging::Info("[TcpSocket] Received %d bytes.", res);
            int32 bid = flags >> IORING_CQE_BUFFER_SHIFT;
            uint8* buf = io_recv_buf_pool[bid];

            recv_context& ctx = args->conn->recv_ctx;
            if (ctx.received_bytes > 0)
            {
                if (ctx.waiting_bytes > received_bytes)
                {
                    std::memcpy(ctx.buffer + ctx.received_bytes, buf, received_bytes);
                    ctx.waiting_bytes -= received_bytes;
                    ctx.received_bytes += received_bytes;
                    io_uring_buf_ring_add(io_recv_buf_ring, buf, MAX_BUFFER_SIZE, bid, io_recv_buf_mask, buf_count++);
                    return;
                }

                received_bytes -= ctx.waiting_bytes;
                processed_bytes = ctx.waiting_bytes;
                std::memcpy(ctx.buffer + ctx.received_bytes, buf, ctx.waiting_bytes);
                ctx.received_bytes = 0;

                if (ctx.packet_bytes == 0)
                {
                    std::memcpy(&ctx.packet_bytes, ctx.buffer, sizeof(packet_size));
                    if (ctx.packet_bytes <= 0 || ctx.packet_bytes > MAX_PACKET_SIZE)
                    {
                        received_bytes = -1;
                        CloseInternal(args->conn);
                    }
                    else
                    {
                        ctx.waiting_bytes = ctx.packet_bytes;
                    }
                }
                else
                {
                    RequestBase* req = request_pool->Acquire(ctx.buffer);
                    req->conn_handle = { args->conn, args->version };
                    while (!requests.Enqueue(req))
                    {
                        std::this_thread::yield();
                    }

                    ctx.packet_bytes = 0;
                    ctx.waiting_bytes = sizeof(packet_size);
                }
            }

            while (ctx.waiting_bytes <= received_bytes)
            {
                if (ctx.packet_bytes == 0)
                {
                    std::memcpy(&ctx.packet_bytes, buf + processed_bytes, sizeof(packet_size));
                    if (ctx.packet_bytes <= 0 || ctx.packet_bytes > MAX_PACKET_SIZE)
                    {
                        received_bytes = -1;
                        CloseInternal(args->conn);
                        break;
                    }

                    ctx.waiting_bytes = ctx.packet_bytes;
                    received_bytes -= sizeof(packet_size);
                    processed_bytes += sizeof(packet_size);
                }
                else
                {
                    RequestBase* req = request_pool->Acquire(buf + processed_bytes);
                    req->conn_handle = { args->conn, args->version };
                    while (!requests.Enqueue(req))
                    {
                        std::this_thread::yield();
                    }

                    received_bytes -= ctx.waiting_bytes;
                    processed_bytes += ctx.waiting_bytes;
                    ctx.packet_bytes = 0;
                    ctx.waiting_bytes = sizeof(packet_size);
                }
            }

            if (received_bytes > 0)
            {
                ctx.received_bytes = received_bytes;
                std::memcpy(ctx.buffer, buf + processed_bytes, received_bytes);
            }

            io_uring_buf_ring_add(io_recv_buf_ring, buf, MAX_BUFFER_SIZE, bid, io_recv_buf_mask, buf_count++);
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            if (received_bytes < 0)
            {
                socket_event_args_pool.Push(args);
            }
            else
            {
                ReceiveInternal(args);
            }
        }
    }
}

inline void TcpServer::OnPoll(socket_event_args* args, int32 res, uint32 flags)
{
    uint8 expected;
    SocketOperationArgs op_args;
    do
    {
        expected = polling.load(std::memory_order_acquire);
        while (socket_op_args.Dequeue(op_args))
        {
            if (op_args.buffer == nullptr)
            {
                for (ConnectionHandle& conn_handle : *(op_args.conn_handles))
                {
                    CloseInternal(conn_handle);
                }
            }
            else
            {
                auto& send_meta = buffer_metadata::get(op_args.buffer, buf_meta_offset).send;
                send_meta.conn_count = op_args.conn_handles->size();
                send_meta.size = op_args.length;
                for (ConnectionHandle& conn_handle : *(op_args.conn_handles))
                {
                    if (conn_handle.version != conn_handle.conn->version)
                    {
                        send_meta.conn_count--;
                        continue;
                    }

                    send_context& ctx = conn_handle.conn->send_ctx;
                    if (ctx.pending_bytes > 0)
                    {
                        // PERF: Merge packets if size less than threshold.
                        ctx.pending_responses.push(op_args.buffer);
                    }
                    else
                    {
                        socket_event_args* args = socket_event_args_pool.Pop();
                        args->op = SEND;
                        args->conn = conn_handle.conn;
                        ctx.buffer = op_args.buffer;
                        ctx.pending_bytes = send_meta.size;
                        ctx.processed_bytes = 0;
                        SendInternal(args, ctx.buffer, send_meta.size);
                    }
                }

                if (send_meta.conn_count <= 0)
                {
                    io_send_buf_pool.Push(op_args.buffer);
                }
            }
        }
    }
    while (polling.compare_exchange_weak(expected, 0, std::memory_order_relaxed));

    if ((flags & IORING_CQE_F_MORE) == 0)
    {
        if (res < 0)
        {
            Logging::Error("[TcpSocket] Failed to poll. Error: %s", strerror(-res));
            socket_event_args_pool.Push(args);
        }
        else
        {
            PollInternal(args);
        }
    }
}

inline void TcpServer::OnSend(socket_event_args* args, int32 res, uint32 flags)
{
    if (args->version != args->conn->version || res < 0)
    {
        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            socket_event_args_pool.Push(args);
        }
    }
    else if (res < 0)
    {
        Logging::Info("[TcpSocket] Connection closed while sending. Op: %d, Error: %s)", args->conn->sock, strerror(-res));
        CloseInternal(args->conn); // PERF: Check if error is fatal.

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            socket_event_args_pool.Push(args);
        }
    }
    else
    {
        send_context& ctx = args->conn->send_ctx;
        if ((flags & IORING_CQE_F_NOTIF) == 0)
        {
            Logging::Info("[TcpSocket] Sent %d bytes.", res);
            ctx.pending_bytes -= res;
            ctx.processed_bytes += res;
        }

        if ((flags & IORING_CQE_F_MORE) == 0)
        {
            if (ctx.pending_bytes > 0)
            {
                SendInternal(args, ctx.buffer + ctx.processed_bytes, ctx.pending_bytes);
                return;
            }

            auto& send_meta = buffer_metadata::get(ctx.buffer, buf_meta_offset).send;
            if (--send_meta.conn_count <= 0)
            {
                io_send_buf_pool.Push(ctx.buffer);
            }

            if (ctx.pending_responses.empty())
            {
                ctx.buffer = nullptr;
                ctx.pending_bytes = 0;
                socket_event_args_pool.Push(args);
            }
            else
            {
                ctx.buffer = ctx.pending_responses.front();
                ctx.pending_bytes = buffer_metadata::get(ctx.buffer, buf_meta_offset).send.size;
                ctx.processed_bytes = 0;
                ctx.pending_responses.pop();
                SendInternal(args, ctx.buffer, ctx.pending_bytes);
            }
        }
    }
}

} // namespace Hypnos
} // namespace Blanketmen