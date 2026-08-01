set(EDITOR_SRC ${CMAKE_SOURCE_DIR}/tools/editor/src)

set(NARAGAME_LUA_DIR ${CMAKE_SOURCE_DIR}/third_party/lua)
if(NOT TARGET lua AND EXISTS ${NARAGAME_LUA_DIR}/lua.h)
    enable_language(C)
    file(GLOB NARAGAME_LUA_SOURCES CONFIGURE_DEPENDS
        ${NARAGAME_LUA_DIR}/*.c)
    list(FILTER NARAGAME_LUA_SOURCES EXCLUDE REGEX
        "/(lua|luac|onelua)\\.c$")
    add_library(lua STATIC ${NARAGAME_LUA_SOURCES})
    target_include_directories(lua PUBLIC ${NARAGAME_LUA_DIR})
    if(MSVC)
        target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS=1)
    endif()
    set_target_properties(lua PROPERTIES FOLDER "third_party")
endif()

set(NARAGAME_PROJECT_RUNTIME_SOURCES
    src/PlayerApp.cpp
    ${EDITOR_SRC}/asset/Animation.cpp
    ${EDITOR_SRC}/asset/AnimatorController.cpp
    ${EDITOR_SRC}/asset/AssetDatabase.cpp
    ${EDITOR_SRC}/asset/AssetGuid.cpp
    ${EDITOR_SRC}/asset/Autotile.cpp
    ${EDITOR_SRC}/asset/Pak.cpp
    ${EDITOR_SRC}/asset/SpriteSheet.cpp
    ${EDITOR_SRC}/asset/TextureImportSettings.cpp
    ${EDITOR_SRC}/asset/Tileset.cpp
    ${EDITOR_SRC}/audio/Audio.cpp
    ${EDITOR_SRC}/project/Project.cpp
    ${EDITOR_SRC}/scene/AnimatorComponent.cpp
    ${EDITOR_SRC}/scene/AudioSourceComponent.cpp
    ${EDITOR_SRC}/scene/BoxColliderComponent.cpp
    ${EDITOR_SRC}/scene/CameraComponent.cpp
    ${EDITOR_SRC}/scene/CapsuleColliderComponent.cpp
    ${EDITOR_SRC}/scene/CircleColliderComponent.cpp
    ${EDITOR_SRC}/scene/Component.cpp
    ${EDITOR_SRC}/scene/EdgeColliderComponent.cpp
    ${EDITOR_SRC}/scene/ParticleSystemComponent.cpp
    ${EDITOR_SRC}/scene/PolygonColliderComponent.cpp
    ${EDITOR_SRC}/scene/Rigidbody2DComponent.cpp
    ${EDITOR_SRC}/scene/Scene.cpp
    ${EDITOR_SRC}/scene/ScriptComponent.cpp
    ${EDITOR_SRC}/scene/LuaComponent.cpp
    ${EDITOR_SRC}/scene/SplashScreenComponent.cpp
    ${EDITOR_SRC}/scene/SpriteRendererComponent.cpp
    ${EDITOR_SRC}/scene/TilemapComponent.cpp
    ${EDITOR_SRC}/scene/TransformComponent.cpp
    ${EDITOR_SRC}/scripting/ScriptHost.cpp)

add_library(NaraGameProjectRuntime STATIC
    ${NARAGAME_PROJECT_RUNTIME_SOURCES})
add_library(NaraGame::ProjectRuntime ALIAS NaraGameProjectRuntime)
target_include_directories(NaraGameProjectRuntime
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${EDITOR_SRC})
target_link_libraries(NaraGameProjectRuntime PUBLIC NaraGame::Engine)
if(TARGET lua)
    target_compile_definitions(NaraGameProjectRuntime PRIVATE
        NARAGAME_SCENE_EDITOR_HAS_LUA=1)
    target_link_libraries(NaraGameProjectRuntime PRIVATE lua)
endif()
set_target_properties(NaraGameProjectRuntime PROPERTIES FOLDER "runtime")

option(NARAGAME_PLAYER_BUILD_EXECUTABLE
    "Build the desktop NaraGamePlayer executable" ON)
if(NARAGAME_PLAYER_BUILD_EXECUTABLE)
    add_executable(NaraGamePlayer src/PlayerMain.cpp)
    target_link_libraries(NaraGamePlayer PRIVATE NaraGame::ProjectRuntime)
    set_target_properties(NaraGamePlayer PROPERTIES FOLDER "tools")
    if(WIN32)
        set_target_properties(NaraGamePlayer PROPERTIES WIN32_EXECUTABLE TRUE)
        if(MSVC)
            target_link_options(NaraGamePlayer PRIVATE "/ENTRY:mainCRTStartup")
        endif()
    endif()
endif()
