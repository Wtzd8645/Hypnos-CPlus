#include <cstdarg>
#include <cstdio>
#include "Logging.hpp"

namespace Blanketmen {
namespace Hypnos {

void Logging::Log(const char* const format, ...)
{
    va_list args{};
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fprintf(stdout, "\n");
}

void Logging::LogWarning(const char* const format, ...)
{
    fprintf(stdout, CONSOLE_LOG_STYLE_REGULAR_YELLOW);
    va_list args{};
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fprintf(stdout, CONSOLE_LOG_STYLE_RESET);
}

void Logging::LogError(const char* const format, ...)
{
    fprintf(stderr, CONSOLE_LOG_STYLE_REGULAR_RED);
    va_list args{};
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, CONSOLE_LOG_STYLE_RESET);
}

} // namespace Hypnos
} // namespace Blanketmen