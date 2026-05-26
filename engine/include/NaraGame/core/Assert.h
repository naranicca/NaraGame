#pragma once

#include "NaraGame/core/Log.h"

#if defined(_MSC_VER)
    #define NARAGAME_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define NARAGAME_DEBUGBREAK() __builtin_trap()
#else
    #include <cstdlib>
    #define NARAGAME_DEBUGBREAK() std::abort()
#endif

#if !defined(NDEBUG)
    // `msg` is a plain const char* describing the failed expectation.
    #define NARAGAME_ASSERT(cond, msg)                                              \
        do {                                                                    \
            if (!(cond)) {                                                      \
                ::NaraGame::Log::Write(::NaraGame::LogLevel::Error,                     \
                                   "Assertion failed (%s): %s", #cond, (msg));  \
                NARAGAME_DEBUGBREAK();                                              \
            }                                                                   \
        } while (0)
#else
    #define NARAGAME_ASSERT(cond, msg) ((void)0)
#endif
