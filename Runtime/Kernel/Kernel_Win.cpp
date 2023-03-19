#include <cstdarg>
#include <cstdio>
#include "Kernel.hpp"

namespace Blanketmen {
namespace Hypnos {

void Kernel::Log(_In_z_ _Printf_format_string_ const char* const format, ...)
{
    va_list args = nullptr;
    va_start(args, format);
    _vfprintf_s_l(stdout, format, nullptr, args); //_vsnprintf_s_l(buffer, 1024, 1024, format, NULL, args);
    va_end(args);
    _vfprintf_l(stdout, "\n", nullptr, nullptr);
}

void Kernel::LogWarning(_In_z_ _Printf_format_string_ const char* const format, ...)
{
    _vfprintf_l(stdout, CONSOLE_LOG_STYLE_REGULAR_YELLOW, nullptr, nullptr);
    va_list args = nullptr;
    va_start(args, format);
    vfprintf(stdout, format, args);
    _vfprintf_s_l(stdout, format, nullptr, args); //_vsnprintf_s_l(buffer, 1024, 1024, format, NULL, args);
    va_end(args);
    _vfprintf_l(stdout, CONSOLE_LOG_STYLE_RESET, nullptr, nullptr);
}

void Kernel::LogError(_In_z_ _Printf_format_string_ const char* const format, ...)
{
    _vfprintf_l(stdout, CONSOLE_LOG_STYLE_REGULAR_RED, nullptr, nullptr);
    va_list args = nullptr;
    va_start(args, format);
    _vfprintf_s_l(stdout, format, nullptr, args); //_vsnprintf_s_l(buffer, 1024, 1024, format, NULL, args);
    va_end(args);
    _vfprintf_l(stdout, CONSOLE_LOG_STYLE_RESET, nullptr, nullptr);
}

} // namespace Hypnos
} // namespace Blanketmen