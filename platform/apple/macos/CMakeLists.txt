cmake_minimum_required(VERSION 3.22)
project(NaraGameMacPlayer LANGUAGES C CXX OBJCXX)

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
add_executable(NaraGamePlayer MACOSX_BUNDLE MacPlayer.mm
    ${NARAGAME_GAME_RESOURCES})
foreach(resource IN LISTS NARAGAME_GAME_RESOURCES)
    file(RELATIVE_PATH resource_rel "${CMAKE_CURRENT_LIST_DIR}/Resources" "${resource}")
    get_filename_component(resource_dir "${resource_rel}" DIRECTORY)
    set_source_files_properties("${resource}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/${resource_dir}")
endforeach()
target_link_libraries(NaraGamePlayer PRIVATE NaraGame::ProjectRuntime)
set_target_properties(NaraGamePlayer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "NaraGame"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.naragame.player"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
