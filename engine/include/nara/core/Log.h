#pragma once

namespace nara {

enum class LogLevel { Trace, Info, Warn, Error };

// Minimal printf-style logger (no external dependency). Writes to stdout/stderr.
class Log {
public:
    static void Init();
    static void Shutdown();

    // `fmt` is a printf-style format string.
    static void Write(LogLevel level, const char* fmt, ...);
};

} // namespace nara

#define NARA_TRACE(...) ::nara::Log::Write(::nara::LogLevel::Trace, __VA_ARGS__)
#define NARA_INFO(...)  ::nara::Log::Write(::nara::LogLevel::Info, __VA_ARGS__)
#define NARA_WARN(...)  ::nara::Log::Write(::nara::LogLevel::Warn, __VA_ARGS__)
#define NARA_ERROR(...) ::nara::Log::Write(::nara::LogLevel::Error, __VA_ARGS__)
