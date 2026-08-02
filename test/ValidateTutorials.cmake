set(samples_dir "${NARAGAME_SOURCE_DIR}/samples")
set(required_samples platformer sky-hopper nightfall-arena ashen-vale turbo-trails)

foreach(sample IN LISTS required_samples)
    set(doc "${samples_dir}/${sample}/README.md")
    if(NOT EXISTS "${doc}")
        message(FATAL_ERROR "Missing sample tutorial: ${doc}")
    endif()
    file(READ "${doc}" contents)
    if(contents STREQUAL "" OR NOT EXISTS "${samples_dir}/${sample}/Assets")
        message(FATAL_ERROR "Incomplete sample: ${sample}")
    endif()
endforeach()

file(READ "${samples_dir}/turbo-trails/README.md" kart)
foreach(topic IN ITEMS "sprite.set_quad" "Race_FixedUpdate"
        "input.gamepad_axis" "드리프트" "AI" "랩")
    string(FIND "${kart}" "${topic}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Turbo Trails is missing: ${topic}")
    endif()
endforeach()

file(READ "${samples_dir}/ashen-vale/README.md" action_rpg)
foreach(topic IN ITEMS "New Game" "Load Game" "Exit" "app.quit()"
        "NPC" "퀘스트" "포탈" "HP" "MP" "인벤토리" "스킬 트리")
    string(FIND "${action_rpg}" "${topic}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Ashen Vale is missing: ${topic}")
    endif()
endforeach()

file(READ "${samples_dir}/README.md" index)
foreach(link IN ITEMS platformer/README.md sky-hopper/README.md
        nightfall-arena/README.md ashen-vale/README.md turbo-trails/README.md)
    string(FIND "${index}" "${link}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Sample index does not link to ${link}")
    endif()
endforeach()

file(READ "${samples_dir}/platformer/README.md" platformer)
string(FIND "${platformer}" "if py > ey + 0.3 and vy <= 0.1 then" stomp_fix)
string(FIND "${platformer}" "MyGame" project_name)
if(stomp_fix EQUAL -1 OR project_name EQUAL -1)
    message(FATAL_ERROR "Platformer regression in stomp or project name")
endif()

set(required_assets
    "platformer/Assets/Sprites/player.png"
    "platformer/Assets/Tilesets/tiles.png"
    "platformer/Assets/Sounds/jump.wav"
    "sky-hopper/Assets/Sprites/glider.png"
    "sky-hopper/Assets/Sprites/digits.png"
    "sky-hopper/Assets/Sounds/flap.wav"
    "nightfall-arena/Assets/Sprites/ranger.png"
    "nightfall-arena/Assets/Sprites/digits.png"
    "nightfall-arena/Assets/Sounds/arena.wav"
    "ashen-vale/Assets/Sprites/world_atlas.png"
    "ashen-vale/Assets/Sounds/dungeon.wav"
    "turbo-trails/Assets/Sprites/Track/track-strip.png")
foreach(asset IN LISTS required_assets)
    if(NOT EXISTS "${samples_dir}/${asset}")
        message(FATAL_ERROR "Missing bundled sample asset: ${asset}")
    endif()
    file(SIZE "${samples_dir}/${asset}" size)
    if(size LESS 44)
        message(FATAL_ERROR "Empty bundled sample asset: ${asset}")
    endif()
endforeach()

set(required_demo_files
    "platformer/CreateDemoProject.ps1"
    "platformer/DemoProject/Assets/Scenes/Level1.nscene"
    "platformer/demo-screenshot.png"
    "sky-hopper/CreateDemoProject.ps1"
    "sky-hopper/DemoProject/Assets/Scenes/Game.nscene"
    "sky-hopper/demo-screenshot.png"
    "nightfall-arena/CreateDemoProject.ps1"
    "nightfall-arena/DemoProject/Assets/Scenes/Arena.nscene"
    "nightfall-arena/demo-screenshot.png"
    "ashen-vale/CreateDemoProject.ps1"
    "ashen-vale/DemoProject/Assets/Scenes/Title.nscene"
    "ashen-vale/DemoProject/Assets/Scenes/Town.nscene"
    "ashen-vale/DemoProject/Assets/Scenes/Dungeon.nscene"
    "ashen-vale/title-screenshot.png"
    "ashen-vale/town-screenshot.png"
    "ashen-vale/dungeon-screenshot.png"
    "turbo-trails/CreateDemoProject.ps1"
    "turbo-trails/DemoProject/Assets/Scenes/Race.nscene"
    "turbo-trails/demo-screenshot.png")
foreach(file IN LISTS required_demo_files)
    if(NOT EXISTS "${samples_dir}/${file}")
        message(FATAL_ERROR "Missing runnable tutorial result: ${file}")
    endif()
endforeach()

foreach(trademark IN ITEMS "Flappy Bird" "Vampire Survivors" "Super Mario")
    foreach(sample IN LISTS required_samples)
        file(READ "${samples_dir}/${sample}/README.md" contents)
        string(FIND "${contents}" "${trademark}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR "${sample} contains trademarked name: ${trademark}")
        endif()
    endforeach()
endforeach()
