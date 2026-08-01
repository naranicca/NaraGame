cmake_minimum_required(VERSION 3.21)

project(NaraGame
    VERSION 0.1.0
    DESCRIPTION "Cross-platform 2D/3D game engine"
    LANGUAGES CXX)

include(CTest)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators default to Debug for first-time configures.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Predictable output locations so the .exe and its console are easy to find.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Organize targets into Solution folders inside Visual Studio.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Sources are UTF-8 (Korean comments). Tell MSVC so it does not fall back to the
# system code page (e.g. 949) and emit C4819 / misread multibyte sequences.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Make the breakout demo the default startup project when a .sln is generated.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT breakout)

if(WIN32)
    message(STATUS "NaraGame: target platform = Windows (D3D11 backend)")
elseif(IOS)
    message(STATUS "NaraGame: target platform = iOS (Metal foundation)")
elseif(APPLE)
    message(STATUS "NaraGame: target platform = macOS (Metal foundation)")
elseif(ANDROID)
    message(STATUS "NaraGame: target platform = Android (Vulkan foundation)")
else()
    message(STATUS "NaraGame: target platform = ${CMAKE_SYSTEM_NAME} (no backend yet)")
endif()

add_subdirectory(engine)
add_subdirectory(test)
add_subdirectory(tools)
