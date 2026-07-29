# ---------------------------------------------------------------------------
# Nara engine — built as a static library that test games link against.
# ---------------------------------------------------------------------------

# Platform-agnostic sources (compiled everywhere).
set(NARAGAME_SOURCES
    src/assets/AssetWatcher.cpp
    src/assets/BMFont.cpp
    src/assets/Dds.cpp
    src/assets/Inflate.cpp
    src/assets/Gltf.cpp
    src/assets/Hdr.cpp
    src/assets/MeshLoader.cpp
    src/assets/MiniJson.cpp
    src/assets/MSDFFontLoader.cpp
    src/assets/SkinnedMeshAnim.cpp
    src/assets/Ntmap.cpp
    src/assets/Png.cpp
    src/assets/TextureLoader.cpp
    src/assets/TileLevel.cpp
    src/assets/Tmx.cpp
    src/audio/AudioClip.cpp
    src/audio/AudioDevice.cpp
    src/core/Application.cpp
    src/core/Log.cpp
    src/debug/DebugDraw.cpp
    src/input/Input.cpp
    src/io/ConfigFile.cpp
    src/physics/PhysicsWorld.cpp
    src/platform/Window.cpp
    src/renderer/BitmapFont.cpp
    src/renderer/Camera2D.cpp
    src/renderer/Font.cpp
    src/renderer/IsoTerrain.cpp
    src/renderer/MSDFFont.cpp
    src/renderer/ParticleSystem.cpp
    src/renderer/Renderer2D.cpp
    src/renderer/PostProcess.cpp
    src/renderer/Renderer3D.cpp
    src/renderer/SpriteSheet.cpp
    src/renderer/TextureAtlas.cpp
    src/renderer/TextureRegion.cpp
    src/renderer/Tilemap.cpp
    src/renderer/rhi/RHIDevice.cpp
    src/scene/SceneManager.cpp
)

# Platform-specific window + RHI backend.
if(WIN32)
    list(APPEND NARAGAME_SOURCES
        src/platform/windows/WindowsWindow.cpp
        src/renderer/rhi/d3d11/D3D11Device.cpp
        src/renderer/rhi/d3d11/D3D11Resources.cpp
        src/audio/xaudio2/XAudio2Device.cpp
    )
elseif(IOS)
    enable_language(OBJCXX)
    list(APPEND NARAGAME_SOURCES
        src/platform/ios/iOSWindow.mm
        src/renderer/rhi/metal/MetalDevice.mm
    )
elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND NARAGAME_SOURCES
        src/platform/macos/MacWindow.mm
        src/renderer/rhi/metal/MetalDevice.mm
    )
elseif(ANDROID)
    enable_language(C)
    list(APPEND NARAGAME_SOURCES
        src/platform/android/AndroidWindow.cpp
        src/renderer/rhi/vulkan/VulkanDevice.cpp
        src/renderer/rhi/vulkan/VulkanResources.cpp
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )
endif()

add_library(NaraGameEngine STATIC ${NARAGAME_SOURCES})
add_library(NaraGame::Engine ALIAS NaraGameEngine)

target_include_directories(NaraGameEngine
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(NaraGameEngine PUBLIC cxx_std_20)

# Keep headers visible in the Visual Studio Solution Explorer.
file(GLOB_RECURSE NARAGAME_HEADERS CONFIGURE_DEPENDS include/*.h src/*.h)
target_sources(NaraGameEngine PRIVATE ${NARAGAME_HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${NARAGAME_SOURCES} ${NARAGAME_HEADERS})

if(WIN32)
    target_compile_definitions(NaraGameEngine PUBLIC
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # Direct3D 11 + DXGI. d3dcompiler/dxguid reserved for upcoming shader work.
    # xaudio2 for audio; ole32 for CoInitialize.
    target_link_libraries(NaraGameEngine PUBLIC d3d11 dxgi d3dcompiler dxguid xaudio2 ole32)
endif()

if(ANDROID)
    target_include_directories(NaraGameEngine PUBLIC
        ${ANDROID_NDK}/sources/android/native_app_glue)
    find_library(NARAGAME_ANDROID_LIB android REQUIRED)
    find_library(NARAGAME_LOG_LIB log REQUIRED)
    find_library(NARAGAME_VULKAN_LIB vulkan REQUIRED)
    target_link_libraries(NaraGameEngine PUBLIC
        ${NARAGAME_ANDROID_LIB}
        ${NARAGAME_LOG_LIB}
        ${NARAGAME_VULKAN_LIB})
    target_compile_definitions(NaraGameEngine PUBLIC
        NARAGAME_HAVE_VULKAN=1)
endif()

if(APPLE)
    target_link_libraries(NaraGameEngine PUBLIC
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation")
    if(IOS)
        target_link_libraries(NaraGameEngine PUBLIC "-framework UIKit")
    else()
        target_link_libraries(NaraGameEngine PUBLIC "-framework AppKit")
    endif()
    target_compile_options(NaraGameEngine PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif()

if(MSVC)
    target_compile_options(NaraGameEngine PRIVATE /W4 /permissive- /MP)
else()
    target_compile_options(NaraGameEngine PRIVATE -Wall -Wextra)
endif()

# Optional Vulkan backend (a second native RHI; runs on Windows when the SDK is
# present). Selected at runtime via the NARAGAME_RHI=vulkan environment variable.
if(NOT ANDROID)
  find_package(Vulkan QUIET)
endif()
if(Vulkan_FOUND AND NOT ANDROID)
    message(STATUS "NaraGame: Vulkan found (${Vulkan_LIBRARY}) -> Vulkan backend enabled")
    target_sources(NaraGameEngine PRIVATE
        src/renderer/rhi/vulkan/VulkanDevice.cpp
        src/renderer/rhi/vulkan/VulkanResources.cpp)
    target_link_libraries(NaraGameEngine PUBLIC Vulkan::Vulkan)
    target_compile_definitions(NaraGameEngine PUBLIC NARAGAME_HAVE_VULKAN)
else()
    message(STATUS "NaraGame: Vulkan SDK not found -> Vulkan backend disabled")
endif()
