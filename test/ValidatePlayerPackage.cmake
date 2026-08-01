file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/project")
file(COPY "${FIXTURE_DIR}/" DESTINATION "${WORK_DIR}/project")

execute_process(
    COMMAND "${EDITOR_EXE}" --project "${WORK_DIR}/project"
            --build-game "${WORK_DIR}/package"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Headless build failed (${result})\n${output}\n${error}")
endif()

foreach(path IN ITEMS
        "${WORK_DIR}/package/game.exe"
        "${WORK_DIR}/package/assets.pak"
        "${WORK_DIR}/package/ProjectSettings/project.json")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Build package is missing ${path}")
    endif()
endforeach()

file(SIZE "${WORK_DIR}/package/game.exe" packaged_size)
file(SIZE "${PLAYER_EXE}" player_size)
if(NOT packaged_size EQUAL player_size)
    message(FATAL_ERROR "game.exe is not the dedicated NaraGamePlayer binary")
endif()

execute_process(
    COMMAND "${WORK_DIR}/package/game.exe" --frames 2
    WORKING_DIRECTORY "${WORK_DIR}/package"
    RESULT_VARIABLE player_result
    OUTPUT_VARIABLE player_output
    ERROR_VARIABLE player_error
    TIMEOUT 15)
if(NOT player_result EQUAL 0)
    message(FATAL_ERROR
        "Packaged player smoke test failed (${player_result})\n"
        "${player_output}\n${player_error}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
