#pragma once

#include "NetworkDefinition.hpp"
#include <Hypnos-Core/Types.hpp>

namespace Blanketmen {
namespace Hypnos {

struct Connection;

class ConnectionPool
{
public:
    class Iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Connection;
        using difference_type = std::ptrdiff_t;
        using pointer = Connection*;
        using reference = Connection&;

        Iterator(Connection* ptr, size_t idx) : ptr(ptr), current(idx) { }

        reference operator*() const { return ptr[ptr[current].index]; }
        pointer operator->() const { return &ptr[ptr[current].index]; }

        Iterator& operator++()
        {
            ++current;
            return *this;
        }

        Iterator operator++(int)
        {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const Iterator& a, const Iterator& b)
        {
            return a.ptr == b.ptr && a.current == b.current;
        }

        friend bool operator!=(const Iterator& a, const Iterator& b)
        {
            return !(a == b);
        }

    private:
        Connection* ptr;
        size_t current;
    };

    ConnectionPool(size_t num) : conns(new Connection[num]), capacity(num)
    {
        for (size_t i = 0; i < num; ++i)
        {
            conns[i].index = i;
        }
    }

    ~ConnectionPool()
    {
        delete[] conns;
    }

    Iterator begin() { return Iterator(conns, 0); }
    Iterator end() { return Iterator(conns, size); }

    void Clear()
    {
        size = 0;
    }

    Connection* Aquire(int32 sock_fd)
    {
        if (size >= capacity)
        {
            return nullptr;
        }

        size_t free = conns[size++].index;
        conns[free].sock_fd = sock_fd;  
        return &conns[free];
    }

    void Release(Connection* conn)
    {
        if (conn->sock_fd <= INVALID_FD)
        {
            return;
        }

        size_t index = conn->index;
        conn->index = conns[--size].index;
        conns[size].index = index;
    }

private:
    Connection* conns;
    size_t capacity;
    size_t size = 0;
};

} // namespace Hypnos
} // namespace Blanketmen