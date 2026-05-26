#pragma once

#include "nara/core/Log.h"

#if defined(_MSC_VER)
    #define NARA_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define NARA_DEBUGBREAK() __builtin_trap()
#else
    #include <cstdlib>
    #define NARA_DEBUGBREAK() std::abort()
#endif

#if !defined(NDEBUG)
    // `msg` is a plain const char* describing the failed expectation.
    #define NARA_ASSERT(cond, msg)                                              \
        do {                                                                    \
            if (!(cond)) {                                                      \
                ::nara::Log::Write(::nara::LogLevel::Error,                     \
                                   "Assertion failed (%s): %s", #cond, (msg));  \
                NARA_DEBUGBREAK();                                              \
            }                                                                   \
        } while (0)
#else
    #define NARA_ASSERT(cond, msg) ((void)0)
#endif
