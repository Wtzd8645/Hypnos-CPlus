#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>


namespace Blanketmen {
namespace Hypnos {

class I18nTextManager
{
public:
    inline static I18nTextManager& Instance() noexcept
    {
        static I18nTextManager instance;
        return instance;
    }

private:
    I18nTextManager() { }
    I18nTextManager(I18nTextManager const&) = delete;
    void operator=(I18nTextManager const&) = delete;

    ~I18nTextManager()
    {
        if (i18nMmapPtr != nullptr)
        {
            ::munmap(i18nMmapPtr, fdStat.st_size);
            ::close(dataFd);
        }
    }

public:
    inline const char8* CurrentLanguage() const noexcept { return currentLanguage; }

    void SetI18nTextDirectoryPath(const char8* i18nTextDirPath);
    void SwitchLanguage(const char8* langName);
    string GetText(uint32 id);

private:
    const char8* FileExt = ".dat";
    const char8* I18nFilePrefix = "I18nText_";
    const int32 TextCountSize = 4;
    const int32 KeySize = 4;
    const int32 InfoTableEntrySize = 8;

    char8* i18nTextDirPath;
    char8* currentLanguage;

    int32 dataFd = -1;
    char8* i18nMmapPtr = nullptr;
    struct stat fdStat;
};

struct NI18nTextArray
{
public:
    inline int32 Length() const noexcept { return length; }

    inline string operator[](uint32 index) const
    {
        if (pointer == nullptr || index >= length)
        {
            //throw new AccessViolationException();
        }
        return I18nTextManager::Instance().GetText(*(pointer + index));
    }

    NI18nTextArray(uint32* ptr, uint32 len)
    {
        pointer = ptr;
        length = len;
    }

private:
    uint32* pointer;
    uint32 length;
};

} // namespace Hypnos
} // namespace Blanketmen