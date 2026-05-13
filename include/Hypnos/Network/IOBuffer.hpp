#pragma once

#include "NetworkConfig.hpp"
#include <Hypnos-Core/Base/Memory/MemoryUtils.hpp>
#include <cassert>
#include <liburing.h>
#include <sys/mman.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class IOBuffer
{
public:
    const uint16 id;
    const uint32 nentries;

    explicit IOBuffer(const IOBufferConfig& cfg) : id(cfg.id), nentries(cfg.nentries)
    {
        mmap_flags |= cfg.mmap_flags;
        page_unit = (mmap_flags & MAP_HUGETLB) ? MemoryUtils::HUGE_PAGE_SIZE : MemoryUtils::PAGE_SIZE;
        segment_size = MemoryUtils::AlignUp(static_cast<size_t>(cfg.buffer_size), alignof(byte*));
    }

    ~IOBuffer()
    {
        Release();
    }

    inline byte* operator[](uint32 bid) const noexcept
    {
        assert(mmap_ptr != nullptr && "[IOBuffer] Mmap is not allocated.");
        assert(bid < nentries && "[IOBuffer] Buffer index out of bounds.");
        return mmap_ptr + bid * segment_size;
    }

    inline Status<void> Initialize(io_uring* io_ring)
    {
        assert(io_ring != nullptr && "[IOBuffer] io_ring cannot be null.");
        assert(buf_ring == nullptr && "[IOBuffer] Buffer ring is already initialized.");
        assert(mmap_ptr == nullptr && "[IOBuffer] Mmap is already allocated.");
        Status<void> status = Allocate();
        if (status.IsFailed())
        {
            return status;
        }

        int32 err = 0;
        buf_ring = io_uring_setup_buf_ring(io_ring, nentries, id, 0, &err);
        if (buf_ring == nullptr)
        {
            Release();
            return Status<void>::Error(ErrorCode::ConfigurationError, "[IOBuffer] Failed to setup buffer ring.");
        }

        ring_mask = io_uring_buf_ring_mask(nentries);
        for (uint32 bid = 0; bid < nentries; ++bid)
        {
            io_uring_buf_ring_add(buf_ring, (*this)[bid], segment_size, bid, ring_mask, pending_count++);
        }

        Flush();
        return Status<void>::Success();
    }

    inline void Return(uint32 bid) noexcept
    {
        assert(bid < nentries && "[IOBuffer] Buffer ID out of bounds.");
        assert(buf_ring != nullptr && "[IOBuffer] Buffer ring is not initialized.");
        io_uring_buf_ring_add(buf_ring, (*this)[bid], segment_size, bid, ring_mask, pending_count++);
    }

    inline void Flush() noexcept
    {
        assert(buf_ring != nullptr && "[IOBuffer] Buffer ring is not initialized.");
        if (pending_count > 0)
        {
            io_uring_buf_ring_advance(buf_ring, pending_count);
            pending_count = 0;
        }
    }

    inline bool HasPending() const noexcept
    {
        return pending_count > 0;
    }

    inline void Destroy(io_uring* io_ring, bool ring_initialized) noexcept
    {
        if (buf_ring != nullptr && io_ring != nullptr && ring_initialized)
        {
            io_uring_free_buf_ring(io_ring, buf_ring, nentries, id);
        }

        buf_ring = nullptr;
        ring_mask = 0;
        pending_count = 0;
        Release();
    }

private:
    io_uring_buf_ring* buf_ring = nullptr;
    int32 ring_mask = 0;
    uint32 pending_count = 0;

    int32 mmap_prot = PROT_READ | PROT_WRITE;
    int32 mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    size_t page_unit = MemoryUtils::PAGE_SIZE;

    size_t segment_size = 0;
    size_t mmap_size = 0;
    byte* mmap_ptr = nullptr;
    size_t capacity = 0;

    inline Status<void> Allocate()
    {
        assert(mmap_ptr == nullptr && "[IOBuffer] Buffer is already allocated.");

        const size_t required_size = segment_size * static_cast<size_t>(nentries);
        mmap_size = MemoryUtils::AlignUp(required_size, page_unit);
        mmap_ptr = static_cast<byte*>(mmap(nullptr, mmap_size, mmap_prot, mmap_flags, -1, 0));
        if (mmap_ptr == MAP_FAILED)
        {
            mmap_ptr = nullptr;
            if ((mmap_flags & MAP_HUGETLB) == 0)
            {
                return Status<void>::Error(ErrorCode::OutOfMemory, "[IOBuffer] Allocation failed.");
            }

            page_unit = MemoryUtils::PAGE_SIZE;
            mmap_size = MemoryUtils::AlignUp(required_size, page_unit);
            mmap_ptr = static_cast<byte*>(mmap(nullptr, mmap_size, mmap_prot, mmap_flags & ~MAP_HUGETLB, -1, 0));
            if (mmap_ptr == MAP_FAILED)
            {
                mmap_ptr = nullptr;
                mmap_size = 0;
                return Status<void>::Error(ErrorCode::OutOfMemory, "[IOBuffer] Allocation failed.");
            }
        }

        if (mmap_size == 0)
        {
            return Status<void>::Error(ErrorCode::OutOfMemory, "[IOBuffer] Allocation failed.");
        }

        capacity = mmap_size / segment_size;
        return Status<void>::Success();
    }

    inline void Release() noexcept
    {
        if (mmap_ptr != nullptr)
        {
            munmap(mmap_ptr, mmap_size);
            mmap_ptr = nullptr;
            mmap_size = 0;
            capacity = 0;
        }
    }
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
