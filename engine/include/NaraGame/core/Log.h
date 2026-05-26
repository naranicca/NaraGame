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

#define NARAGAME_TRACE(...) ::NaraGame::Log::Write(::NaraGame::LogLevel::Trace, __VA_ARGS__)
#define NARAGAME_INFO(...)  ::NaraGame::Log::Write(::NaraGame::LogLevel::Info, __VA_ARGS__)
#define NARAGAME_WARN(...)  ::NaraGame::Log::Write(::NaraGame::LogLevel::Warn, __VA_ARGS__)
#define NARAGAME_ERROR(...) ::NaraGame::Log::Write(::NaraGame::LogLevel::Error, __VA_ARGS__)
