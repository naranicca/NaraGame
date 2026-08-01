cmake_minimum_required(VERSION 3.22)
project(NaraGameIOSPlayer LANGUAGES C CXX OBJCXX)

set(NARAGAME_APP_NAME "NaraGame")
set(NARAGAME_APP_ID "com.naragame.player")
set(NARAGAME_APP_VERSION "0.1.0")
include("${CMAKE_CURRENT_LIST_DIR}/NaraGameBuildSettings.cmake" OPTIONAL)

if(NOT IOS)
    message(FATAL_ERROR "Configure with -DCMAKE_SYSTEM_NAME=iOS -G Xcode")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(NARAGAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_subdirectory("${NARAGAME_ROOT}/engine"
                 "${CMAKE_CURRENT_BINARY_DIR}/naragame-engine")
set(NARAGAME_PLAYER_BUILD_EXECUTABLE OFF CACHE BOOL "" FORCE)
add_subdirectory("${NARAGAME_ROOT}/tools/player"
                 "${CMAKE_CURRENT_BINARY_DIR}/naragame-player-runtime")

file(GLOB_RECURSE NARAGAME_GAME_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/Resources/*")
add_executable(NaraGamePlayer MACOSX_BUNDLE IOSPlayer.mm
    ${NARAGAME_GAME_RESOURCES})
foreach(resource IN LISTS NARAGAME_GAME_RESOURCES)
    file(RELATIVE_PATH resource_rel "${CMAKE_CURRENT_LIST_DIR}/Resources" "${resource}")
    get_filename_component(resource_dir "${resource_rel}" DIRECTORY)
    set_source_files_properties("${resource}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/${resource_dir}")
endforeach()
target_link_libraries(NaraGamePlayer PRIVATE NaraGame::ProjectRuntime
    "-framework UIKit" "-framework Metal" "-framework QuartzCore")
set_target_properties(NaraGamePlayer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "${NARAGAME_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${NARAGAME_APP_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${NARAGAME_APP_VERSION}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${NARAGAME_APP_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
