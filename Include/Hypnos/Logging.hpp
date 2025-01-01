#pragma once

namespace Blanketmen {
namespace Hypnos {

constexpr const char* CONSOLE_LOG_STYLE_RESET          = "\033[0;0m\n";
constexpr const char* CONSOLE_LOG_STYLE_REGULAR_NORMAL = "\033[0;0m";
constexpr const char* CONSOLE_LOG_STYLE_REGULAR_BLACK  = "\033[0;30m";
constexpr const char* CONSOLE_LOG_STYLE_REGULAR_RED    = "\033[0;31m";
constexpr const char* CONSOLE_LOG_STYLE_REGULAR_GREEN  = "\033[0;32m";
constexpr const char* CONSOLE_LOG_STYLE_REGULAR_YELLOW = "\033[0;33m";
constexpr const char* CONSOLE_LOG_STYLE_REGULAR_BLUE   = "\033[0;34m";
constexpr const char* CONSOLE_LOG_STYLE_REGULAR_PURPLE = "\033[0;35m";
constexpr const char* CONSOLE_LOG_STYLE_REGULAR_CYAN   = "\033[0;36m";
constexpr const char* CONSOLE_LOG_STYLE_REGULAR_WHITE  = "\033[0;37m";

class Logging
{
public:
#if defined _WIN32
    static void Info(_In_z_ _Printf_format_string_ const char* const format, ...);
    static void Warning(_In_z_ _Printf_format_string_ const char* const format, ...);
    static void Error(_In_z_ _Printf_format_string_ const char* const format, ...);
#elif defined __linux__
    static void Info(const char* const format, ...) noexcept;
    static void Warning(const char* const format, ...) noexcept;
    static void Error(const char* const format, ...) noexcept;
#endif
};

} // namespace Hypnos
} // namespace Blanketmen