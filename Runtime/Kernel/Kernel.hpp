#ifndef KERNEL_HPP_
#define KERNEL_HPP_

namespace Blanketmen {
namespace Hypnos {

class Kernel
{
#pragma region Logger
#define CONSOLE_LOG_STYLE_REGULAR_NORMAL "\033[0;0m"
#define CONSOLE_LOG_STYLE_REGULAR_BLACK  "\033[0;30m"
#define CONSOLE_LOG_STYLE_REGULAR_RED    "\033[0;31m"
#define CONSOLE_LOG_STYLE_REGULAR_GREEN  "\033[0;32m"
#define CONSOLE_LOG_STYLE_REGULAR_YELLOW "\033[0;33m"
#define CONSOLE_LOG_STYLE_REGULAR_BLUE   "\033[0;34m"
#define CONSOLE_LOG_STYLE_REGULAR_PURPLE "\033[0;35m"
#define CONSOLE_LOG_STYLE_REGULAR_CYAN   "\033[0;36m"
#define CONSOLE_LOG_STYLE_REGULAR_WHITE  "\033[0;37m"

#define CONSOLE_LOG_STYLE_RESET "\033[0;0m\n"

public:
#if defined _WIN32
    static void Log(_In_z_ _Printf_format_string_ const char* const format, ...);
    static void LogWarning(_In_z_ _Printf_format_string_ const char* const format, ...);
    static void LogError(_In_z_ _Printf_format_string_ const char* const format, ...);
#elif defined __linux__
    static void Log(const char* const format, ...);
    static void LogWarning(const char* const format, ...);
    static void LogError(const char* const format, ...);
#endif
#pragma endregion
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // KERNEL_HPP_