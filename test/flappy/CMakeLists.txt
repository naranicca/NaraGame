add_executable(flappy src/FlappyApp.cpp)
target_link_libraries(flappy PRIVATE NaraGame::Engine)
set_target_properties(flappy PROPERTIES FOLDER "test")

target_compile_definitions(flappy PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
