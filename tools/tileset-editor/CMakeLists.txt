add_executable(tileset-editor src/EditorApp.cpp)
target_link_libraries(tileset-editor PRIVATE NaraGame::NaraGame)
set_target_properties(tileset-editor PROPERTIES FOLDER "tools")

target_compile_definitions(tileset-editor PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
