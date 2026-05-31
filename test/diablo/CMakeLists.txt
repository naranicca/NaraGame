add_executable(diablo src/DiabloApp.cpp)
target_link_libraries(diablo PRIVATE NaraGame::NaraGame)
set_target_properties(diablo PROPERTIES FOLDER "test")

target_compile_definitions(diablo PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
