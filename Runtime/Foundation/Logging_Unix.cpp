#include <cstdarg>
#include <cstdio>
#include "Logging.hpp"

namespace Blanketmen {
namespace Hypnos {

void Logging::Log(const char* const format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    vfprintf(stdout, "\n", nullptr);
}

void Logging::LogWarning(const char* const format, ...)
{
    vfprintf(stdout, CONSOLE_LOG_STYLE_REGULAR_YELLOW, nullptr);
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    vfprintf(stdout, CONSOLE_LOG_STYLE_RESET, nullptr);
}

void Logging::LogError(const char* const format, ...)
{
    vfprintf(stderr, CONSOLE_LOG_STYLE_REGULAR_RED, nullptr);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    vfprintf(stderr, CONSOLE_LOG_STYLE_RESET, nullptr);
}

} // namespace Hypnos
} // namespace Blanketmen