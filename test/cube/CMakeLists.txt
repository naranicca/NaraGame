add_executable(cube src/CubeApp.cpp)
target_link_libraries(cube PRIVATE NaraGame::Engine)
target_compile_definitions(cube PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
