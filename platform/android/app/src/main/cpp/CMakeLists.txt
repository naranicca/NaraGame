cmake_minimum_required(VERSION 3.22.1)
project(NaraGameAndroid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This template lives at platform/android/app/src/main/cpp. Keep the engine
# source in one place while developing; exported SDK builds can replace this
# path with a copied engine source/runtime package.
get_filename_component(NARAGAME_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
add_subdirectory("${NARAGAME_ROOT}/engine"
                 "${CMAKE_CURRENT_BINARY_DIR}/naragame-engine")

add_library(naragame SHARED AndroidPlayer.cpp)
target_link_libraries(naragame PRIVATE NaraGame::Engine)
