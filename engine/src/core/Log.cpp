#include "NaraGame/core/Log.h"

#include <cstdarg>
#include <cstdio>

namespace NaraGame {

// Always-on file log next to the running exe. Opened at program
// startup (namespace-scope static -> runs before main via the CRT),
// so even a Release build that emits zero WARN/ERROR lines still
// leaves a nara-log.txt for post-mortem. Silent no-op if fopen
// fails (read-only working directory, etc.).
static std::FILE* g_logFile = std::fopen("nara-log.txt", "w");

static const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

void Log::Init() {}
void Log::Shutdown() {}

void Log::Write(LogLevel level, const char* fmt, ...) {
    std::FILE* out = (level == LogLevel::Error || level == LogLevel::Warn) ? stderr : stdout;
    std::FILE* logf = g_logFile;

    std::fprintf(out, "[NaraGame][%s] ", LevelName(level));
    if (logf) std::fprintf(logf, "[NaraGame][%s] ", LevelName(level));

    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    std::vfprintf(out, fmt, args);
    if (logf) std::vfprintf(logf, fmt, args_copy);
    va_end(args_copy);
    va_end(args);

    std::fputc('\n', out);
    std::fflush(out); // flush each line so logs survive a crash / kill
    if (logf) {
        std::fputc('\n', logf);
        std::fflush(logf);
    }
}

} // namespace NaraGame
