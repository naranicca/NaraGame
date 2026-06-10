add_executable(zelda src/ZeldaApp.cpp)
target_link_libraries(zelda PRIVATE NaraGame::NaraGame)
set_target_properties(zelda PROPERTIES FOLDER "test")

target_compile_definitions(zelda PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
