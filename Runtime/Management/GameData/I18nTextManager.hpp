#ifndef I18N_TEXT_MANAGER_HPP_
#define I18N_TEXT_MANAGER_HPP_

#if defined _WIN32

#elif defined __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

#include <Core/Runtime/Type.hpp>

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
    inline const_char_ptr CurrentLanguage() const noexcept { return currentLanguage; }

    void SetI18nTextDirectoryPath(const_char_ptr i18nTextDirPath);
    void SwitchLanguage(const_char_ptr langName);
    string GetText(uint32 id);

private:
    const_char_ptr FileExt = ".dat";
    const_char_ptr I18nFilePrefix = "I18nText_";
    const int32 TextCountSize = 4;
    const int32 KeySize = 4;
    const int32 InfoTableEntrySize = 8;

    char_ptr i18nTextDirPath;
    char_ptr currentLanguage;

    int32 dataFd = -1;
    char_ptr i18nMmapPtr = nullptr;
    struct stat fdStat;
};

struct NI18nTextArray
{
public:
    inline int32 Length() const noexcept { return length; }

    inline string operator[](size_t index) const
    {
        if (pointer == nullptr || index < 0 || index >= length)
        {
            //throw new AccessViolationException();
        }
        return I18nTextManager::Instance().GetText(*(pointer + index));
    }

    NI18nTextArray(uint32* ptr, int32 len)
    {
        pointer = ptr;
        length = len;
    }

private:
    int32 length;
    uint32* pointer;
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // I18N_TEXT_MANAGER_HPP_