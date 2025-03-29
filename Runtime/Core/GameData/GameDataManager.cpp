#include "GameDataManager.hpp"
#include <cstring>

namespace Blanketmen {
namespace Hypnos {

uint32 BkdrHash(const uint8* bytes, int32 size)
{
    uint32 hash = 0;
    for (int32 i = 0; i < size; ++i)
    {
        hash = hash * 31 + bytes[i];
    }
    return hash;
}

void GameDataManager::CreateMmap(const char8* dataFilePath)
{
    if (dataMmapPtr != nullptr)
    {
        ::munmap(dataMmapPtr, fdStat.st_size);
        ::close(dataFd);
    }

    dataFd = ::open(dataFilePath, O_RDONLY, S_IRWXU);
    ::fstat(dataFd, &fdStat);
    dataMmapPtr = reinterpret_cast<uint8*>(::mmap(0, fdStat.st_size, PROT_READ, MAP_SHARED, dataFd, 0));
    if (dataMmapPtr == MAP_FAILED)
    {
        Logging::Info("[GameDataManager] Create mmap fail: %d", errno);
        dataMmapPtr = nullptr;
        return;
    }

    Logging::Info("[GameDataManager] Create mmap successfully.");
}

int32 GameDataManager::GetKeyTableOffset(const char8* tableName)
{
    const int32 NameOffsetField = 4;
    const int32 TableOffsetField = 8;
    const int32 NextOffsetField = 20;

    uint32 hash = BkdrHash(reinterpret_cast<const uint8*>(tableName), ::strlen(tableName));
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

uint8* GameDataManager::GetDataPointer(int32 keyTableOffset, int32 key)
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

uint8* GameDataManager::GetDataPointer(int32 offset)
{
    return offset > InvalidOffset ? dataMmapPtr + offset : nullptr;
}

inline string GameDataManager::GetString(int32 offset)
{
    if (offset <= InvalidOffset)
    {
        return string();
    }

    char8* strPtr = reinterpret_cast<char8*>(dataMmapPtr + offset);
    return string(strPtr + StrCountSize, *reinterpret_cast<int32*>(strPtr));
}

} // namespace Hypnos
} // namespace Blanketmen