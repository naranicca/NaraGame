add_executable(brawl src/BrawlApp.cpp)
target_link_libraries(brawl PRIVATE NaraGame::Engine)
set_target_properties(brawl PROPERTIES FOLDER "test")

target_compile_definitions(brawl PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
