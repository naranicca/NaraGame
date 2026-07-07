#pragma once

namespace NaraGame {

enum class LogLevel { Trace, Info, Warn, Error };

// Minimal printf-style logger (no external dependency). Writes to stdout/stderr.
class Log {
public:
    static void Init();
    static void Shutdown();

    // `fmt` is a printf-style format string.
    static void Write(LogLevel level, const char* fmt, ...);
};

} // namespace NaraGame

// Release builds define NDEBUG (CMake sets it automatically for
// Release / MinSizeRel). Trace + Info are development-time signal
// and get compiled out there -- the arguments aren't evaluated, so
// side-effect-free logging incurs zero cost. Warn / Error stay live
// in every configuration because they mark real problems the user
// or a QA engineer needs to see.
#if defined(NDEBUG)
#  define NARAGAME_TRACE(...) ((void)0)
#  define NARAGAME_INFO(...)  ((void)0)
#else
#  define NARAGAME_TRACE(...) ::NaraGame::Log::Write(::NaraGame::LogLevel::Trace, __VA_ARGS__)
#  define NARAGAME_INFO(...)  ::NaraGame::Log::Write(::NaraGame::LogLevel::Info, __VA_ARGS__)
#endif
#define NARAGAME_WARN(...)  ::NaraGame::Log::Write(::NaraGame::LogLevel::Warn, __VA_ARGS__)
#define NARAGAME_ERROR(...) ::NaraGame::Log::Write(::NaraGame::LogLevel::Error, __VA_ARGS__)
