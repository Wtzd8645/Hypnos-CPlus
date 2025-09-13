#pragma once

#include <Hypnos-Kernel/Base/Memory/MemoryUtils.hpp>
#include <Hypnos-Kernel/Container/ForwardList.hpp>
#include <Hypnos-Kernel/Container/List.hpp>
#include <sys/mman.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class IOBufferPool
{
public:
    static constexpr size_t MIN_BUFFER_SIZE = 1024;
    static constexpr size_t MIN_BUFFER_CAPACITY = 32;

    IOBufferPool(size_t size, size_t cap, int32 flags)
    {
        this->size = MemoryUtils::AlignUp(size > MIN_BUFFER_SIZE ? size : MIN_BUFFER_SIZE, alignof(uint8*));
        mmap_flags |= flags;
        Allocate(1);// cap > MIN_BUFFER_CAPACITY ? cap : MIN_BUFFER_CAPACITY);
    }

    ~IOBufferPool()
    {
        for (auto& chunk : chunks)
        {
            munmap(chunk.ptr, chunk.size);
        }
    }

    inline uint8* operator[](int32 index) const noexcept { return segments[index]; }

    inline int32 Capacity() const noexcept { return capacity; }

    inline void Allocate(int32 count)
    {
        size_t mmap_size = size * count;
        uint8* ptr = static_cast<uint8*>(mmap(nullptr, mmap_size, mmap_prot, mmap_flags, -1, 0));
        if (ptr == MAP_FAILED)
        {
            Logging::Error("[IOBufferPool] Failed to allocate IO buffer pool. %s", strerror(errno));
            throw std::bad_alloc();
        }

        capacity += count;
        chunks.push_front({ ptr, mmap_size });

        segments.reserve(capacity);
        for (int32 i = 1; i < count; ++i)
        {
            segments.push_back(ptr + i * size);
        }
    }

private:
    struct MmapChunk
    {
        uint8* ptr;
        size_t size;
    };

    size_t size = 0;
    int32 mmap_prot = PROT_READ | PROT_WRITE;
    int32 mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;

    size_t capacity = 0;
    ForwardList<MmapChunk> chunks;
    List<uint8*> segments;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen