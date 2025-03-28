#include "Hypnos/Network/NetworkManager.hpp"
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {

NetworkConfig NetworkManager::config;

void NetworkManager::Initialize()
{
    config.max_conns = 8192; // TODO: Calculate max num of conns.
    io_ctx = new io_uring_context(config.max_conns);
    if (io_uring_queue_init_params(config.max_conns, &io_ctx->ring, &io_ctx->ring_params) < 0)
    {
        throw std::runtime_error("[TcpSocket] Failed to initialize io_uring.");
    }

    int32 err;
    io_uring_buf_ring* recv_buf_ring = io_uring_setup_buf_ring(&io_ctx->ring, config.max_conns, IO_RECV_BUF_GROUP, 0, &err);
    if (recv_buf_ring == nullptr)
    {
        throw std::runtime_error("[TcpSocket] Failed to setup buf ring. Error: " + std::to_string(err));
    }

    io_ctx->recv_buf_ring = recv_buf_ring;
    io_ctx->recv_buf_mask = io_uring_buf_ring_mask(config.max_conns);
    for (int32 i = 0; i < config.max_conns; ++i)
    {
        io_uring_buf_ring_add(recv_buf_ring, io_ctx->recv_buf_pool[i], MAX_BUFFER_SIZE, i, io_ctx->recv_buf_mask, i);
    }
    io_uring_buf_ring_advance(recv_buf_ring, config.max_conns);

    // TODO: Set sockets.
}

void NetworkManager::Release()
{
    if (io_ctx == nullptr)
    {
        return;
    }

    io_uring_queue_exit(&io_ctx->ring);
    io_uring_free_buf_ring(&io_ctx->ring, io_ctx->recv_buf_ring, config.max_conns, 0);
    io_ctx->recv_buf_ring = nullptr;

    delete io_ctx;
    io_ctx = nullptr;

    // TODO: Close all sockets and notify.
}

void NetworkManager::ProcessEvents()
{
    int32 res;
    io_uring_cqe* cqes;
    uint32 cq_head;
    io_uring_cqe* cqe;
    running.store(true, std::memory_order_relaxed);
    while (running)
    {
        res = io_uring_wait_cqe(&io_ctx->ring, &cqes);
        if (res < 0)
        {
            OnCqeError(-res);
            continue;
        }

        io_uring_for_each_cqe(&io_ctx->ring, cq_head, cqe)
        {
            io_event_args* args = static_cast<io_event_args*>(io_uring_cqe_get_data(cqe));
            sockets[args->sock_id]->ProcessEvent(args, cqe->res, cqe->flags);
        }

        io_ctx->advance_buf_ring();
        io_uring_cq_advance(&io_ctx->ring, cq_head - io_ctx->ring.cq.khead[0]);
    }

    Release();
}

void NetworkManager::OnCqeError(int32 err)
{
    static const std::unordered_set<int> retriable_errors = { EIO, EAGAIN, ENOMEM, EBUSY };

    if (retriable_errors.contains(err))
    {
        Logging::Warning("[TcpSocket] Failed to wait for CQE. Error: %s", strerror(err));
        usleep(8); // TODO: Make configurable.
    }
    else
    {
        Logging::Error("[TcpSocket] Failed to wait for CQE.");
        running.store(false, std::memory_order_release);
    }
}

} // namespace Hypnos
} // namespace Blanketmen