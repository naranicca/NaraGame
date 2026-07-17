add_executable(mario src/MarioApp.cpp)
target_link_libraries(mario PRIVATE NaraGame::Engine)
set_target_properties(mario PROPERTIES FOLDER "test")

target_compile_definitions(mario PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
