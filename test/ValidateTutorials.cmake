set(docs_dir "${NARAGAME_SOURCE_DIR}/tools/editor/docs")
set(required_docs
    README.md
    tutorial-mario-platformer.md
    tutorial-sky-hopper.md
    tutorial-nightfall-arena.md)

foreach(doc IN LISTS required_docs)
    if(NOT EXISTS "${docs_dir}/${doc}")
        message(FATAL_ERROR "Missing tutorial document: ${doc}")
    endif()
    file(READ "${docs_dir}/${doc}" contents)
    if(contents STREQUAL "")
        message(FATAL_ERROR "Empty tutorial document: ${doc}")
    endif()
endforeach()

file(READ "${docs_dir}/README.md" index)
foreach(doc IN ITEMS tutorial-mario-platformer.md tutorial-sky-hopper.md
                     tutorial-nightfall-arena.md)
    string(FIND "${index}" "${doc}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Tutorial index does not link to ${doc}")
    endif()
endforeach()

file(READ "${docs_dir}/tutorial-mario-platformer.md" platformer)
string(FIND "${platformer}" "if py > ey + 0.3 and vy <= 0.1 then" stomp_fix)
if(stomp_fix EQUAL -1)
    message(FATAL_ERROR "Platformer stomp must accept a collision-resolved vy of zero")
endif()
string(FIND "${platformer}" "MyGame" project_name)
if(project_name EQUAL -1)
    message(FATAL_ERROR "Platformer tutorial must use the MyGame project name")
endif()

foreach(trademark IN ITEMS "Flappy Bird" "Vampire Survivors" "Super Mario")
    foreach(doc IN ITEMS tutorial-mario-platformer.md tutorial-sky-hopper.md
                         tutorial-nightfall-arena.md)
        file(READ "${docs_dir}/${doc}" contents)
        string(FIND "${contents}" "${trademark}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR "${doc} contains trademarked name: ${trademark}")
        endif()
    endforeach()
endforeach()
