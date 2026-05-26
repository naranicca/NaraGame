#include "NaraGame/core/Log.h"

#include <cstdarg>
#include <cstdio>

namespace NaraGame {

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

    std::fprintf(out, "[NaraGame][%s] ", LevelName(level));

    va_list args;
    va_start(args, fmt);
    std::vfprintf(out, fmt, args);
    va_end(args);

    std::fputc('\n', out);
    std::fflush(out); // flush each line so logs survive a crash / kill
}

} // namespace NaraGame
