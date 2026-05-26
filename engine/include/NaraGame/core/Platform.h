#pragma once

// Detects the target platform and defines exactly one NARAGAME_PLATFORM_* macro.
// Derived purely from compiler-provided macros so it works under CMake, Xcode,
// and the Android NDK without extra configuration.

#if defined(_WIN32) || defined(_WIN64)
    #define NARAGAME_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
        #define NARAGAME_PLATFORM_IOS 1
    #elif TARGET_OS_MAC
        #define NARAGAME_PLATFORM_MACOS 1
    #else
        #error "Unknown Apple platform"
    #endif
#elif defined(__ANDROID__)
    #define NARAGAME_PLATFORM_ANDROID 1
#elif defined(__linux__)
    #define NARAGAME_PLATFORM_LINUX 1
#else
    #error "Unknown platform"
#endif
