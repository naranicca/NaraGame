cmake_minimum_required(VERSION 3.22)
project(NaraGameMacPlayer LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(NARAGAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_subdirectory("${NARAGAME_ROOT}/engine"
                 "${CMAKE_CURRENT_BINARY_DIR}/naragame-engine")

add_executable(NaraGamePlayer MACOSX_BUNDLE MacPlayer.mm)
target_link_libraries(NaraGamePlayer PRIVATE NaraGame::Engine)
set_target_properties(NaraGamePlayer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "NaraGame"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.naragame.player"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
