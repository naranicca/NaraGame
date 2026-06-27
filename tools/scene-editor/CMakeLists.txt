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
    src/project/Project.cpp)
target_include_directories(scene-editor PRIVATE src)
target_link_libraries(scene-editor PRIVATE NaraGame::NaraGame imgui)
set_target_properties(scene-editor PROPERTIES FOLDER "tools")
