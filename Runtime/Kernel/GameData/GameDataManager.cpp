#include <cstring>
#include "../Kernel.hpp"
#include "GameDataManager.hpp"

namespace Blanketmen {
namespace Hypnos {

uint32 BkdrHash(const_uint8_ptr bytes, int32 size)
{
    uint32 hash = 0;
    for (int32 i = 0; i < size; ++i)
    {
        hash = hash * 31 + bytes[i];
    }
    return hash;
}

void GameDataManager::CreateMmap(const_char_ptr dataFilePath)
{
    if (dataMmapPtr != nullptr)
    {
        ::munmap(dataMmapPtr, fdStat.st_size);
        ::close(dataFd);
    }

    dataFd = ::open(dataFilePath, O_RDONLY, S_IRWXU);
    ::fstat(dataFd, &fdStat);
    dataMmapPtr = reinterpret_cast<char_ptr>(::mmap(0, fdStat.st_size, PROT_READ, MAP_SHARED, dataFd, 0));
    if (dataMmapPtr == MAP_FAILED)
    {
        Kernel::Log("[GameDataManager] Create mmap fail: %d", errno);
        dataMmapPtr = nullptr;
        return;
    }

    Kernel::Log("[GameDataManager] Create mmap successfully.");
}

int32 GameDataManager::GetKeyTableOffset(const_char_ptr tableName)
{
    const int32 NameOffsetField = 4;
    const int32 TableOffsetField = 8;
    const int32 NextOffsetField = 20;

    uint32 hash = BkdrHash(reinterpret_cast<const_uint8_ptr>(tableName), ::strlen(tableName));
    int32 capacity = *reinterpret_cast<int32*>(dataMmapPtr);
    int32 slot = (int32)(hash % (uint32)capacity);
    int32 offset = TableCapacitySize + slot * InfoTableEntrySize;
    while (offset > InvalidOffset)
    {
        if (hash == *reinterpret_cast<uint32*>(dataMmapPtr + offset))
        {
            int32 strOffset = *reinterpret_cast<int32*>(dataMmapPtr + offset + NameOffsetField);
            if (tableName == GetString(strOffset))
            {
                return *reinterpret_cast<int32*>(dataMmapPtr + offset + TableOffsetField);
            }
        }
        offset = *reinterpret_cast<int32*>(dataMmapPtr + offset + NextOffsetField);
    }
    return InvalidOffset;
}

char_ptr GameDataManager::GetDataPointer(int32 keyTableOffset, int32 key)
{
    const int32 DataOffsetField = 4;
    const int32 NextOffsetField = 8;

    int32 capacity = *reinterpret_cast<int32*>(dataMmapPtr + keyTableOffset);
    int32 slot = (int32)(key % (uint32)capacity);
    int32 offset = keyTableOffset + TableCapacitySize + slot * KeyTableEntrySize;
    while (offset > InvalidOffset)
    {
        if (key == *reinterpret_cast<int32*>(dataMmapPtr + offset))
        {
            return dataMmapPtr + *reinterpret_cast<int32*>(dataMmapPtr + offset + DataOffsetField);
        }
        offset = *reinterpret_cast<int32*>(dataMmapPtr + offset + NextOffsetField);
    }
    return nullptr;
}

char_ptr GameDataManager::GetDataPointer(int32 offset)
{
    return offset > InvalidOffset ? dataMmapPtr + offset : nullptr;
}

inline string GameDataManager::GetString(int32 offset)
{
    if (offset <= InvalidOffset)
    {
        return string();
    }

    char_ptr strPtr = dataMmapPtr + offset;
    return string(strPtr + StrCountSize, *reinterpret_cast<int32*>(strPtr));
}

} // namespace Hypnos
} // namespace Blanketmen