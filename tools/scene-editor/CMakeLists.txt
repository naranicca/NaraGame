# ---------------------------------------------------------------------------
# scene-editor -- Unity-style scene editor built on Dear ImGui + NaraGame RHI.
# ---------------------------------------------------------------------------

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
    message(WARNING
        "scene-editor: third_party/imgui is empty. Run "
        "`git submodule update --init --recursive` to fetch Dear ImGui. "
        "Skipping the scene-editor target until then.")
    return()
endif()

# Compile ImGui as a small static lib alongside the editor. We keep the
# demo TU in so the editor can ShowDemoWindow() for sanity checks; the
# linker drops it once the editor stops calling it.
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler)
set_target_properties(imgui PROPERTIES FOLDER "third_party")
if(MSVC)
    # ImGui's TUs aren't UTF-8 source; turn off /utf-8 for them to silence
    # C4828 warnings and keep build output clean.
    target_compile_options(imgui PRIVATE /W3)
endif()

add_executable(scene-editor
    src/EditorApp.cpp
    src/asset/Animation.cpp
    src/asset/AnimatorController.cpp
    src/asset/AssetDatabase.cpp
    src/asset/AssetGuid.cpp
    src/asset/Autotile.cpp
    src/asset/SpriteSheet.cpp
    src/asset/TextureImportSettings.cpp
    src/asset/Tileset.cpp
    src/project/Project.cpp
    src/scene/AnimatorComponent.cpp
    src/scene/Component.cpp
    src/scene/Scene.cpp
    src/scene/ScriptComponent.cpp
    src/scene/SpriteRendererComponent.cpp
    src/scene/TilemapComponent.cpp
    src/scene/TransformComponent.cpp
    src/scripting/ScriptHost.cpp)
target_include_directories(scene-editor PRIVATE src)
target_link_libraries(scene-editor PRIVATE NaraGame::NaraGame imgui)
set_target_properties(scene-editor PROPERTIES FOLDER "tools")

# ---------------------------------------------------------------------------
# Optional Lua 5.4 scripting -- vendored sources live in third_party/lua/.
# Until the user drops the Lua sources in place ScriptHost stays a stub
# (M6c1 behaviour); once lua.h is present we build the core + libs as a
# small static lib and let ScriptHost.cpp's Lua branch take over. See
# third_party/lua/README.md for the exact file list the cmake glob below
# expects.
# ---------------------------------------------------------------------------
set(LUA_DIR ${CMAKE_SOURCE_DIR}/third_party/lua)
if(EXISTS ${LUA_DIR}/lua.h)
    # The root project declares CXX only; enable C here so the Lua
    # sources find the right toolchain without disturbing the rest.
    enable_language(C)
    set(LUA_SOURCES
        ${LUA_DIR}/lapi.c
        ${LUA_DIR}/lauxlib.c
        ${LUA_DIR}/lbaselib.c
        ${LUA_DIR}/lcode.c
        ${LUA_DIR}/lcorolib.c
        ${LUA_DIR}/lctype.c
        ${LUA_DIR}/ldblib.c
        ${LUA_DIR}/ldebug.c
        ${LUA_DIR}/ldo.c
        ${LUA_DIR}/ldump.c
        ${LUA_DIR}/lfunc.c
        ${LUA_DIR}/lgc.c
        ${LUA_DIR}/linit.c
        ${LUA_DIR}/liolib.c
        ${LUA_DIR}/llex.c
        ${LUA_DIR}/lmathlib.c
        ${LUA_DIR}/lmem.c
        ${LUA_DIR}/loadlib.c
        ${LUA_DIR}/lobject.c
        ${LUA_DIR}/lopcodes.c
        ${LUA_DIR}/loslib.c
        ${LUA_DIR}/lparser.c
        ${LUA_DIR}/lstate.c
        ${LUA_DIR}/lstring.c
        ${LUA_DIR}/lstrlib.c
        ${LUA_DIR}/ltable.c
        ${LUA_DIR}/ltablib.c
        ${LUA_DIR}/ltm.c
        ${LUA_DIR}/lundump.c
        ${LUA_DIR}/lutf8lib.c
        ${LUA_DIR}/lvm.c
        ${LUA_DIR}/lzio.c)
    add_library(lua STATIC ${LUA_SOURCES})
    target_include_directories(lua PUBLIC ${LUA_DIR})
    set_target_properties(lua PROPERTIES FOLDER "third_party")
    if(MSVC)
        target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS=1)
        target_compile_options(lua PRIVATE /W3)
    endif()
    target_compile_definitions(scene-editor PRIVATE NARAGAME_SCENE_EDITOR_HAS_LUA=1)
    target_link_libraries(scene-editor PRIVATE lua)
    message(STATUS "scene-editor: Lua 5.4 sources found -- building real ScriptHost.")
else()
    message(STATUS "scene-editor: third_party/lua is empty. Vendor Lua 5.4 "
                   "sources (see third_party/lua/README.md) to enable scripting; "
                   "ScriptHost stays a stub until then.")
endif()
