#pragma once

#include <Hypnos-Kernel/Base.hpp>
#include <Hypnos-Kernel/Core/Container.hpp>
#include <forward_list>
#include <sys/mman.h>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

class IOUringBufferPool
{
public:
    static constexpr size_t MIN_BUFFER_SIZE = 1024;

    IOUringBufferPool(size_t buf_size, int32 flags, size_t cap = 8)
    {
        if (buf_size < MIN_BUFFER_SIZE)
        {
            buf_size = MIN_BUFFER_SIZE;
        }

        this->buf_size = (buf_size + alignof(uint8*) - 1) & ~(alignof(uint8*) - 1);
        mmap_flags |= flags;
        Allocate(cap > 8 ? cap : 8);
    }

    ~IOUringBufferPool()
    {
        for (auto& block : blocks)
        {
            munmap(block.ptr, block.size);
        }
    }

    inline uint8* operator[](int32 index) const noexcept { return segments[index]; }

    inline int32 Capacity() const noexcept { return capacity; }

    void Allocate(int32 count)
    {
        size_t mmap_size = buf_size * count;
        uint8* ptr = static_cast<uint8*>(mmap(nullptr, mmap_size, mmap_prot, mmap_flags, -1, 0));
        if (ptr == MAP_FAILED)
        {
            throw std::bad_alloc();
        }

        capacity += count;
        blocks.push_front({ ptr, mmap_size });

        for (int32 i = 1; i < count; ++i)
        {
            segments.push_back(ptr + buf_size * i);
        }
    }

private:
    struct MmapBlock
    {
        uint8* ptr;
        size_t size;
    };

    size_t buf_size = 0;
    int32 mmap_prot = PROT_READ | PROT_WRITE;
    int32 mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;

    size_t capacity;
    std::forward_list<MmapBlock, Memory::MemoryPoolAllocator<MmapBlock>> blocks;
    Container::List<uint8*> segments;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen