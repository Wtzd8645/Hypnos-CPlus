#include "NetworkDefs.hpp"
#include "NetworkManager.hpp"
#include "TcpServer.hpp"
#include <unordered_set>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

NetworkConfig NetworkManager::config;

void NetworkManager::Initialize()
{
    uint32 max_conns = 0;
    for (auto& cfg : config.server_configs)
    {
        max_conns += cfg.max_conns;
    }

    io_ctx = new IOContext(max_conns);
    if (io_ctx->Setup() != 0)
    {
        throw std::runtime_error("[NetworkManager] Failed to setup io_uring context.");
    }

    servers.reserve(config.server_configs.size());
    for (auto& cfg : config.server_configs)
    {
        switch (cfg.protocol)
        {
            case TransportProtocol::TCP:
            {
                servers[cfg.id] = new TcpServer(cfg, *io_ctx);
                break;
            }
            default:
            {
                throw std::runtime_error("[NetworkManager] Unsupported transport protocol.");
            }
        }
    }

    for (auto& server : servers)
    {
        if (server != nullptr)
        {
            sockets.push_back(server);
        }
    }

    io_thread = new Thread([this]()
    {
        ProcessIOEvents();
    });
}

void NetworkManager::Release()
{
    if (io_thread != nullptr)
    {
        running.store(false, std::memory_order_release);
        io_thread->join();
        delete io_thread;
        io_thread = nullptr;
    }

    if (io_ctx != nullptr)
    {
        delete io_ctx;
        io_ctx = nullptr;
    }

    for (auto& sock : sockets)
    {
        delete sock;
    }
    sockets.clear();
}

void NetworkManager::ProcessIOEvents()
{
    io_uring* ring = &io_ctx->ring;
    int32 res;
    io_uring_cqe* cqes;
    uint32 head;
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

        io_uring_for_each_cqe(ring, head, cqe)
        {
            IOEventArgs* args = static_cast<IOEventArgs*>(io_uring_cqe_get_data(cqe));
            sockets[args->sock_id]->ProcessIOEvent(args, cqe->res, cqe->flags);
        }

        io_ctx->AdvanceBufRing();
        io_ctx->AdvanceCqeRing(head);
    }
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
        Logging::Error("[TcpSocket] Failed to wait for CQE. %s", strerror(err));
        running.store(false, std::memory_order_release);
    }
}

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen