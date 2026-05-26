add_executable(space_invaders src/SpaceInvadersApp.cpp)
target_link_libraries(space_invaders PRIVATE NaraGame::NaraGame)
set_target_properties(space_invaders PROPERTIES FOLDER "test")

target_compile_definitions(space_invaders PRIVATE
    NARAGAME_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
