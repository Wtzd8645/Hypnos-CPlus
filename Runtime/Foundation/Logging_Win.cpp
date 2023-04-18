#include <cstdarg>
#include <cstdio>
#include "Logging.hpp"

namespace Blanketmen {
namespace Hypnos {

void Logging::Log(_In_z_ _Printf_format_string_ const char* const format, ...)
{
    va_list args = nullptr;
    va_start(args, format);
    _vfprintf_s_l(stdout, format, nullptr, args); //_vsnprintf_s_l(buffer, 1024, 1024, format, NULL, args);
    va_end(args);
    _vfprintf_l(stdout, "\n", nullptr, nullptr);
}

void Logging::LogWarning(_In_z_ _Printf_format_string_ const char* const format, ...)
{
    _vfprintf_l(stdout, CONSOLE_LOG_STYLE_REGULAR_YELLOW, nullptr, nullptr);
    va_list args = nullptr;
    va_start(args, format);
    vfprintf(stdout, format, args);
    _vfprintf_s_l(stdout, format, nullptr, args); //_vsnprintf_s_l(buffer, 1024, 1024, format, NULL, args);
    va_end(args);
    _vfprintf_l(stdout, CONSOLE_LOG_STYLE_RESET, nullptr, nullptr);
}

void Logging::LogError(_In_z_ _Printf_format_string_ const char* const format, ...)
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