#include "NetworkManager.hpp"
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

NetworkConfig NetworkManager::config;

void NetworkManager::Initialize()
{
    config.max_conns = 8192; // TODO: Calculate max num of conns.
    io_ctx = new IOUringContext(config.max_conns);
    io_ctx->Setup();

    // TODO: Set sockets.
}

void NetworkManager::Release()
{
    if (io_ctx == nullptr)
    {
        return;
    }

    delete io_ctx;
    io_ctx = nullptr;

    for (auto& sock : sockets)
    {
        delete sock;
    }
}

void NetworkManager::ProcessEvents()
{
    io_uring* ring = &io_ctx->ring;
    int32 res;
    io_uring_cqe* cqes;
    uint32 cq_head;
    io_uring_cqe* cqe;
    running.store(true, std::memory_order_relaxed);
    while (running)
    {
        res = io_uring_wait_cqe(ring, &cqes);
        if (res < 0)
        {
            OnCqeError(-res);
            continue;
        }

        io_uring_for_each_cqe(ring, cq_head, cqe)
        {
            IOEventArgs* args = static_cast<IOEventArgs*>(io_uring_cqe_get_data(cqe));
            sockets[args->sock_id]->ProcessEvent(args, cqe->res, cqe->flags);
        }

        io_ctx->AdvanceBufRing();
        io_ctx->AdvanceCqRing(cq_head);
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

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen