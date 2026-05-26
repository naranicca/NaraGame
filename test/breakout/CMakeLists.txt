add_executable(breakout src/BreakoutApp.cpp)
target_link_libraries(breakout PRIVATE NaraGame::NaraGame)
set_target_properties(breakout PROPERTIES FOLDER "test")

# Bake the source assets path so the game finds its sprites regardless of the
# working directory (handy for development / debugging in Visual Studio).
target_compile_definitions(breakout PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
