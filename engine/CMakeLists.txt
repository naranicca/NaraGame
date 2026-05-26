# ---------------------------------------------------------------------------
# Nara engine — built as a static library that test games link against.
# ---------------------------------------------------------------------------

# Platform-agnostic sources (compiled everywhere).
set(NARA_SOURCES
    src/assets/BMFont.cpp
    src/assets/Inflate.cpp
    src/assets/Png.cpp
    src/assets/TextureLoader.cpp
    src/assets/TileLevel.cpp
    src/audio/AudioClip.cpp
    src/audio/AudioDevice.cpp
    src/core/Application.cpp
    src/core/Log.cpp
    src/input/Input.cpp
    src/io/ConfigFile.cpp
    src/platform/Window.cpp
    src/renderer/BitmapFont.cpp
    src/renderer/Camera2D.cpp
    src/renderer/Font.cpp
    src/renderer/Renderer2D.cpp
    src/renderer/SpriteSheet.cpp
    src/renderer/TextureAtlas.cpp
    src/renderer/TextureRegion.cpp
    src/renderer/Tilemap.cpp
    src/renderer/rhi/RHIDevice.cpp
    src/scene/SceneManager.cpp
)

# Platform-specific window + RHI backend.
if(WIN32)
    list(APPEND NARA_SOURCES
        src/platform/windows/WindowsWindow.cpp
        src/renderer/rhi/d3d11/D3D11Device.cpp
        src/renderer/rhi/d3d11/D3D11Resources.cpp
        src/audio/xaudio2/XAudio2Device.cpp
    )
elseif(IOS)
    list(APPEND NARA_SOURCES
        src/platform/ios/iOSWindow.cpp
        src/renderer/rhi/metal/MetalDevice.cpp
    )
elseif(APPLE)
    list(APPEND NARA_SOURCES
        src/platform/macos/MacWindow.cpp
        src/renderer/rhi/metal/MetalDevice.cpp
    )
elseif(ANDROID)
    list(APPEND NARA_SOURCES
        src/platform/android/AndroidWindow.cpp
        src/renderer/rhi/vulkan/VulkanDevice.cpp
    )
endif()

add_library(nara STATIC ${NARA_SOURCES})
add_library(Nara::nara ALIAS nara)

target_include_directories(nara
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(nara PUBLIC cxx_std_20)

# Keep headers visible in the Visual Studio Solution Explorer.
file(GLOB_RECURSE NARA_HEADERS CONFIGURE_DEPENDS include/*.h src/*.h)
target_sources(nara PRIVATE ${NARA_HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${NARA_SOURCES} ${NARA_HEADERS})

if(WIN32)
    target_compile_definitions(nara PUBLIC
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # Direct3D 11 + DXGI. d3dcompiler/dxguid reserved for upcoming shader work.
    # xaudio2 for audio; ole32 for CoInitialize.
    target_link_libraries(nara PUBLIC d3d11 dxgi d3dcompiler dxguid xaudio2 ole32)
endif()

if(MSVC)
    target_compile_options(nara PRIVATE /W4 /permissive- /MP)
else()
    target_compile_options(nara PRIVATE -Wall -Wextra)
endif()

# Optional Vulkan backend (a second native RHI; runs on Windows when the SDK is
# present). Selected at runtime via the NARA_RHI=vulkan environment variable.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Nara: Vulkan found (${Vulkan_LIBRARY}) -> Vulkan backend enabled")
    target_sources(nara PRIVATE
        src/renderer/rhi/vulkan/VulkanDevice.cpp
        src/renderer/rhi/vulkan/VulkanResources.cpp)
    target_link_libraries(nara PUBLIC Vulkan::Vulkan)
    target_compile_definitions(nara PUBLIC NARA_HAVE_VULKAN)
else()
    message(STATUS "Nara: Vulkan SDK not found -> Vulkan backend disabled")
endif()
