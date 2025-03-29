#pragma once

#include <Hypnos-Kernel/Base.hpp>
#include <Hypnos-Kernel/Core/Container.hpp>
#include <forward_list>
#include <sys/mman.h>

namespace Blanketmen {
namespace Hypnos {
namespace Cache {

class IoUringBufferPool
{
public:
    static constexpr size_t MIN_BUFFER_SIZE = 1024;

    IoUringBufferPool(int32 size, int32 flags, int32 cap = 8)
    {
        if (size < MIN_BUFFER_SIZE)
        {
            size = MIN_BUFFER_SIZE;
        }

        this->size = (size + alignof(uint8*) - 1) & ~(alignof(uint8*) - 1);
        mmap_flags |= flags;
        Allocate(cap > 8 ? cap : 8);
    }

    ~IoUringBufferPool()
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
        size_t mmap_size = size * count;
        uint8* ptr = static_cast<uint8*>(mmap(nullptr, mmap_size, mmap_prot, mmap_flags, -1, 0));
        if (ptr == MAP_FAILED)
        {
            throw std::bad_alloc();
        }

        capacity += count;
        blocks.push_front({ ptr, mmap_size });

        for (int32 i = 1; i < count; ++i)
        {
            segments.push_back(ptr + size * i);
        }
    }

private:
    struct mmap_block
    {
        uint8* ptr;
        size_t size;
    };

    int32 size = 0;
    int32 mmap_prot = PROT_READ | PROT_WRITE;
    int32 mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;

    int32 capacity;
    std::forward_list<mmap_block, Memory::MemoryPoolAllocator<mmap_block>> blocks;
    Container::List<uint8*> segments;
};

} // namespace Cache
} // namespace Hypnos
} // namespace Blanketmen