#pragma once

#include "NetworkRuntimeDefs.hpp"

#include <Hypnos-Core/Container/List.hpp>

#include <cassert>

namespace Blanketmen {
namespace Hypnos {
namespace Network {

template<typename T>
class BoundedQueue
{
public:
    void Initialize(size_t queue_capacity)
    {
        assert(queue_capacity > 0);

        items.clear();
        items.resize(queue_capacity);
        head = 0;
        tail = 0;
        count = 0;
    }

    bool Enqueue(const T& item)
    {
        if (IsFull())
        {
            return false;
        }

        items[tail] = item;
        tail = (tail + 1) % items.size();
        ++count;
        return true;
    }

    bool Dequeue(T& item)
    {
        if (IsEmpty())
        {
            return false;
        }

        item = items[head];
        head = (head + 1) % items.size();
        --count;
        return true;
    }

    void Clear() noexcept
    {
        head = 0;
        tail = 0;
        count = 0;
    }

    bool IsEmpty() const noexcept { return count == 0; }
    bool IsFull() const noexcept { return !items.empty() && count == items.size(); }
    size_t Capacity() const noexcept { return items.size(); }

private:
    List<T> items;
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;
};

} // namespace Network
} // namespace Hypnos
} // namespace Blanketmen
