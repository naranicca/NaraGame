# Test games built on top of the Nara engine. Each is its own executable.
add_subdirectory(breakout)
add_subdirectory(space_invaders)
add_subdirectory(mario)
add_subdirectory(citybuilder)
add_subdirectory(diablo)
add_subdirectory(flappy)
add_subdirectory(brawl)
add_subdirectory(zelda)
add_subdirectory(cube)

add_test(NAME editor_tutorial_docs
    COMMAND ${CMAKE_COMMAND}
        -DNARAGAME_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/ValidateTutorials.cmake)

add_test(NAME editor_build_game_package
    COMMAND ${CMAKE_COMMAND}
        -DEDITOR_EXE=$<TARGET_FILE:NaraGame>
        -DPLAYER_EXE=$<TARGET_FILE:NaraGamePlayer>
        -DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/player-project
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/player-package-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/ValidatePlayerPackage.cmake)
