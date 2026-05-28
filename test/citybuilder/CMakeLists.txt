add_executable(citybuilder src/CitybuilderApp.cpp)
target_link_libraries(citybuilder PRIVATE NaraGame::NaraGame)
set_target_properties(citybuilder PROPERTIES FOLDER "test")

target_compile_definitions(citybuilder PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
