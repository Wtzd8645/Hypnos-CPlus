#include "Hypnos/Logging.hpp"
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Blanketmen {
namespace Hypnos {

void Logging::Info(const char* const format, ...) noexcept
{
    va_list args{};
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fprintf(stdout, "\n");
}

void Logging::Warning(const char* const format, ...) noexcept
{
    fprintf(stdout, CONSOLE_LOG_STYLE_REGULAR_YELLOW);
    va_list args{};
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fprintf(stdout, CONSOLE_LOG_STYLE_RESET);
}

void Logging::Error(const char* const format, ...) noexcept
{
    fprintf(stderr, CONSOLE_LOG_STYLE_REGULAR_RED);
    va_list args{};
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    if (errno != 0)
    {
        fprintf(stderr, "Error: %s", strerror(errno));
    }
    fprintf(stderr, CONSOLE_LOG_STYLE_RESET);
}

} // namespace Hypnos
} // namespace Blanketmen