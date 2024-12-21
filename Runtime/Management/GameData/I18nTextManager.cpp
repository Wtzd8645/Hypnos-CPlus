#include <cstring>
#include <Foundation/Logging.hpp>
#include "I18nTextManager.hpp"

namespace Blanketmen {
namespace Hypnos {

void I18nTextManager::SetI18nTextDirectoryPath(const_char_ptr i18nTextDirPath)
{
    this->i18nTextDirPath = const_cast<char_ptr>(i18nTextDirPath);
}

void I18nTextManager::SwitchLanguage(const_char_ptr langName)
{
    if (currentLanguage == langName)
    {
        return;
    }

    if (i18nMmapPtr != nullptr)
    {
        ::munmap(i18nMmapPtr, fdStat.st_size);
        ::close(dataFd);
    }

    string filePath = string(i18nTextDirPath).append("/").append(I18nFilePrefix).append(langName).append(FileExt);
    dataFd = ::open(filePath.c_str(), O_RDONLY, S_IRWXU);
    ::fstat(dataFd, &fdStat);
    i18nMmapPtr = reinterpret_cast<char_ptr>(::mmap(0, fdStat.st_size, PROT_READ, MAP_SHARED, dataFd, 0));
    if (i18nMmapPtr == MAP_FAILED)
    {
        Logging::Info("[I18nTextManager] Create mmap fail: %d", errno);
        i18nMmapPtr = nullptr;
        return;
    }

    Logging::Info("[I18nTextManager] Create mmap successfully.");
    currentLanguage = const_cast<char_ptr>(langName);
}

string I18nTextManager::GetText(uint32 id)
{
    if (i18nMmapPtr == nullptr)
    {
        return std::to_string(id);
    }

    int32 min = 0;
    int32 max = *reinterpret_cast<int32*>(i18nMmapPtr) - 1;
    while (min <= max)
    {
        int32 mid = (min + max) / 2;
        int32 offset = TextCountSize + mid * InfoTableEntrySize;
        uint32 key = *reinterpret_cast<uint32*>(i18nMmapPtr + offset);
        if (id == key)
        {
            char_ptr textPtr = i18nMmapPtr + *reinterpret_cast<int32*>(i18nMmapPtr + offset + KeySize);
            return string(textPtr + TextCountSize, *reinterpret_cast<int32*>(textPtr));
        }

        if (id < key)
        {
            max = mid - 1;
        }
        else
        {
            min = mid + 1;
        }
    }
    return string();
}

} // namespace Hypnos
} // namespace Blanketmen