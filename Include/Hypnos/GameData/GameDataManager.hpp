#pragma once

#if defined _WIN32

#elif defined __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

#include <Hypnos-Core/Type.hpp>

namespace Blanketmen {
namespace Hypnos {

class GameDataManager
{
public:
    inline static GameDataManager& Instance() noexcept
    {
        static GameDataManager instance;
        return instance;
    }

private:
    GameDataManager() { }
    GameDataManager(GameDataManager const&) = delete;
    void operator=(GameDataManager const&) = delete;

    ~GameDataManager()
    {
        if (dataMmapPtr != nullptr)
        {
            ::munmap(dataMmapPtr, fdStat.st_size);
            ::close(dataFd);
        }
    }

public:
    void CreateMmap(const_char_ptr dataFilePath);
    int32 GetKeyTableOffset(const_char_ptr tableName);
    char_ptr GetDataPointer(int32 keyTableOffset, int32 key);
    char_ptr GetDataPointer(int32 offset);
    string GetString(int32 offset);

private:
    const int32 InvalidOffset = -1;

    const int32 StrCountSize = 4;
    const int32 TableCapacitySize = 4;
    const int32 InfoTableEntrySize = 24;
    const int32 KeyTableEntrySize = 12;

    int32 dataFd = -1;
    char_ptr dataMmapPtr = nullptr;
    struct stat fdStat;
};

template<class T>
struct NArray
{
public:
    inline int32 Length() const noexcept { return length; }

    inline T operator[](size_t index) const
    {
        if (pointer == nullptr || index < 0 || index >= length)
        {
            // throw new AccessViolationException();
        }
        return *(pointer + index);
    }

    NArray(T* ptr, int32 len)
    {
        pointer = ptr;
        length = len;
    }

private:
    T* pointer;
    int32 length;
};

template<class T>
struct NStructArray
{
public:
    inline int32 Length() const noexcept { return length; }

    inline T operator[](size_t index) const
    {
        if (pointer == nullptr || index < 0 || index >= length)
        {
            // throw new AccessViolationException();
        }

        return T(pointer + index * structSize);
    }

    NStructArray(char_ptr ptr, int32 size, int32 len)
    {
        pointer = ptr;
        structSize = size;
        length = len;
    }

private:
    int32 length;
    char_ptr pointer;
    int32 structSize;
};

struct NStringArray
{
public:
    inline int32 Length() const noexcept { return length; }

    inline string operator[](size_t index) const
    {
        if (pointer == nullptr || index < 0 || index >= length)
        {
            // throw new AccessViolationException();
        }
        return GameDataManager::Instance().GetString(*(pointer + index));
    }

    NStringArray(int32* ptr, int32 len)
    {
        pointer = ptr;
        length = len;
    }

private:
    int32 length;
    int32* pointer;
};

template<class T>
struct NReferenceArray
{
public:
    inline int32 Length() const noexcept { return length; }

    inline T operator[](size_t index) const
    {
        if (pointer == nullptr || index < 0 || index >= length)
        {
            // throw new AccessViolationException();
        }
        return T(GameDataManager::Instance().GetDataPointer(*(pointer + index)));
    }

    NReferenceArray(int32* ptr, int32 len)
    {
        pointer = ptr;
        length = len;
    }

private:
    int32 length;
    int32* pointer;
};

} // namespace Hypnos
} // namespace Blanketmen