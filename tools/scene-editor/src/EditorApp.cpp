// scene-editor -- Unity-style scene editor shell. Runs on top of the
// NaraGame Application loop: the host drives the frame, ImGui's Win32
// backend gets WM_* events via Window::SetMessageHook, and ImGui's
// D3D11 backend draws on top of the engine's backbuffer. M1 commit 4
// adds Entity / Transform / Scene save+load + Hierarchy / Inspector
// panels on top of the project shell.

#include "asset/AssetDatabase.h"
#include "asset/TextureImportSettings.h"
#include "project/Project.h"
#include "scene/Component.h"
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "scene/SpriteRendererComponent.h"
#include "scene/TransformComponent.h"

#include <NaraGame/renderer/rhi/RHIResources.h>

#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/platform/Window.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder* APIs
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

#include <d3d11.h>
#include <shobjidl.h>          // IFileOpenDialog
#include <combaseapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Forward-declared by imgui_impl_win32.h with C++ linkage. We wrap it
// through the engine's untyped MessageHook signature below.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace NaraGame;

namespace {

constexpr uint32_t kWindowW = 1600;
constexpr uint32_t kWindowH = 900;
constexpr const char* kBaseTitle = "NaraGame Scene Editor";

// Hook routed through Window::SetMessageHook. Always feeds ImGui first
// so its IO stays in sync; returns false so the engine's WM_SIZE /
// WM_CLOSE bookkeeping still runs.
bool ImGuiMessageHook(void* nativeHandle, unsigned int msg,
                      unsigned long long wparam,
                      long long lparam) {
    HWND hwnd  = reinterpret_cast<HWND>(nativeHandle);
    WPARAM wp  = static_cast<WPARAM>(wparam);
    LPARAM lp  = static_cast<LPARAM>(lparam);
    ImGui_ImplWin32_WndProcHandler(hwnd, static_cast<UINT>(msg), wp, lp);
    return false;
}

// Open the Win32 folder picker (IFileOpenDialog with FOS_PICKFOLDERS).
// Returns "" if the user cancels or anything fails. UTF-8 out.
std::string PickFolderDialog(HWND owner, const wchar_t* title) {
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return {};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (title) dlg->SetTitle(title);
    hr = dlg->Show(owner);
    if (FAILED(hr)) { dlg->Release(); return {}; }
    IShellItem* item = nullptr;
    if (FAILED(dlg->GetResult(&item)) || !item) { dlg->Release(); return {}; }
    PWSTR wpath = nullptr;
    item->GetDisplayName(SIGDN_FILESYSPATH, &wpath);
    item->Release();
    dlg->Release();
    if (!wpath) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1,
                                        nullptr, 0, nullptr, nullptr);
    std::string out;
    if (len > 1) {
        out.resize(size_t(len) - 1);
        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out.data(),
                            len, nullptr, nullptr);
    }
    CoTaskMemFree(wpath);
    // Normalise back-slashes so the rest of the editor sees one style.
    for (char& c : out) if (c == '\\') c = '/';
    return out;
}

// Convert a UTF-8 path returned by the shell into the engine's
// forward-slash normalised form.
std::string WideToUtf8(PCWSTR ws) {
    if (!ws) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1,
                                        nullptr, 0, nullptr, nullptr);
    std::string out;
    if (len > 1) {
        out.resize(size_t(len) - 1);
        WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(),
                            len, nullptr, nullptr);
    }
    for (char& c : out) if (c == '\\') c = '/';
    return out;
}

// File open / save dialog for one extension. `extName` shows up as the
// filter description; `extPattern` is the wildcard (e.g. L"*.nscene");
// `defaultExt` is appended when the user doesn't type one. Returns ""
// on cancel.
std::string PickFileDialog(HWND owner, REFCLSID clsid,
                           const wchar_t* title,
                           const wchar_t* extName,
                           const wchar_t* extPattern,
                           const wchar_t* defaultExt,
                           const std::string& initialDir = {}) {
    IFileDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return {};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM);
    if (title) dlg->SetTitle(title);
    COMDLG_FILTERSPEC filter[1] = { { extName, extPattern } };
    dlg->SetFileTypes(1, filter);
    if (defaultExt) dlg->SetDefaultExtension(defaultExt);
    if (!initialDir.empty()) {
        // Convert UTF-8 -> wide so SHCreateItemFromParsingName accepts it.
        const int wlen = MultiByteToWideChar(CP_UTF8, 0, initialDir.c_str(),
                                             -1, nullptr, 0);
        if (wlen > 1) {
            std::wstring wide(size_t(wlen) - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, initialDir.c_str(), -1,
                                wide.data(), wlen);
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr,
                                                      IID_PPV_ARGS(&folder)))) {
                dlg->SetFolder(folder);
                folder->Release();
            }
        }
    }
    hr = dlg->Show(owner);
    if (FAILED(hr)) { dlg->Release(); return {}; }
    IShellItem* item = nullptr;
    if (FAILED(dlg->GetResult(&item)) || !item) { dlg->Release(); return {}; }
    PWSTR wpath = nullptr;
    item->GetDisplayName(SIGDN_FILESYSPATH, &wpath);
    item->Release();
    dlg->Release();
    std::string out = WideToUtf8(wpath);
    if (wpath) CoTaskMemFree(wpath);
    return out;
}

void SetWindowTitle(HWND hwnd, const std::string& title) {
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1,
                                         nullptr, 0);
    if (wlen <= 0) return;
    std::wstring wtitle(size_t(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1,
                        wtitle.data(), wlen);
    SetWindowTextW(hwnd, wtitle.c_str());
}

class EditorApp : public Application {
public:
    EditorApp(std::string startupProject, std::string startupScene)
        : Application(MakeConfig()),
          m_startupProjectPath(std::move(startupProject)),
          m_startupScenePath(std::move(startupScene)) {}

    void OnStart() override {
        // COM apartment for IFileOpenDialog. ole32 is already linked
        // by the engine for XAudio2 / D3D11 init paths.
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        HWND hwnd = static_cast<HWND>(GetWindow().NativeHandle());
        auto* dev = static_cast<ID3D11Device*>(GetDevice().GetNativeDevice());
        auto* ctx = static_cast<ID3D11DeviceContext*>(GetDevice().GetNativeContext());
        if (!hwnd || !dev || !ctx) {
            NARAGAME_ERROR("scene-editor: missing native window or D3D11 device handles");
            Close();
            return;
        }
        m_hwnd = hwnd;

        IMGUI_CHECKVERSION();
        m_imguiContext = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = "scene-editor.ini";
        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(dev, ctx);

        GetWindow().SetMessageHook(&ImGuiMessageHook);

        // Register the built-in component factories before anything tries
        // to deserialize a scene.
        SceneEditor::RegisterBuiltinComponents();

        UpdateWindowTitle();

        NARAGAME_INFO("scene-editor: ImGui %s + DX11 + dockspace ready",
                      ImGui::GetVersion());

        // Command-line auto-open. `--project <path>` opens a project at
        // startup; `--scene <path>` follows to load a scene inside it.
        // Wire-up for headless smoke tests + handy when re-launching
        // from an external editor / build script.
        if (!m_startupProjectPath.empty()) {
            SceneEditor::Project p;
            if (p.Open(m_startupProjectPath)) {
                m_project = std::move(p);
                m_assetDb.Open(m_project);
                UpdateWindowTitle();
            }
        }
        if (!m_startupScenePath.empty() && m_scene.Load(m_startupScenePath)) {
            m_currentScenePath = m_startupScenePath;
            UpdateWindowTitle();
        }
    }

    void OnStop() override {
        GetWindow().SetMessageHook(nullptr);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        if (m_imguiContext) {
            ImGui::DestroyContext(m_imguiContext);
            m_imguiContext = nullptr;
        }
        CoUninitialize();
    }

    void OnRender() override {
        GetDevice().Clear(Color(0.10f, 0.11f, 0.13f, 1.0f));

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const ImGuiID dockId =
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        EnsureDefaultLayout(dockId);

        DrawMainMenuBar();
        DrawHierarchyPanel();
        DrawInspectorPanel();
        DrawViewportPanel();
        DrawConsolePanel();
        DrawProjectPanel();
        DrawNewProjectModal();
        if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

private:
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title  = kBaseTitle;
        c.width  = kWindowW;
        c.height = kWindowH;
        return c;
    }

    void UpdateWindowTitle() {
        if (!m_hwnd) return;
        std::string title = kBaseTitle;
        if (m_project.IsOpen()) {
            title += " - ";
            title += m_project.Name();
            title += " : ";
            title += m_scene.Name();
            if (m_scene.IsDirty()) title += "*";
        }
        SetWindowTitle(m_hwnd, title);
    }

    // --- Project flow ----------------------------------------------------

    void StartNewProjectFlow() {
        std::snprintf(m_newProjectName.data(), m_newProjectName.size(),
                      "MyGameProject");
        m_newProjectPath[0] = '\0';
        m_newProjectError.clear();
        m_openNewProjectModal = true;
    }

    void DoOpenProject() {
        const std::string folder =
            PickFolderDialog(m_hwnd, L"Open NaraGame Project");
        if (folder.empty()) return;
        SceneEditor::Project p;
        if (!p.Open(folder)) return;
        m_project = std::move(p);
        m_assetDb.Open(m_project);
        UpdateWindowTitle();
    }

    void DoCloseProject() {
        if (!m_project.IsOpen()) return;
        m_project.Close();
        m_assetDb.Close();
        m_scene = SceneEditor::Scene();
        m_currentScenePath.clear();
        m_selection = SceneEditor::kInvalidEntityId;
        UpdateWindowTitle();
    }

    // --- Scene flow -----------------------------------------------------

    std::string ScenesDir() const {
        return m_project.IsOpen() ? (m_project.AssetsDir() + "/Scenes")
                                  : std::string{};
    }

    void DoNewScene() {
        m_scene = SceneEditor::Scene();
        m_scene.SetName("Untitled");
        m_currentScenePath.clear();
        m_selection = SceneEditor::kInvalidEntityId;
        UpdateWindowTitle();
    }

    void DoOpenScene() {
        const std::string path = PickFileDialog(
            m_hwnd, CLSID_FileOpenDialog, L"Open Scene",
            L"NaraGame Scene (*.nscene)", L"*.nscene", L"nscene",
            ScenesDir());
        if (path.empty()) return;
        if (m_scene.Load(path)) {
            m_currentScenePath = path;
            m_selection = SceneEditor::kInvalidEntityId;
            UpdateWindowTitle();
        }
    }

    void DoSaveScene() {
        if (m_currentScenePath.empty()) { DoSaveSceneAs(); return; }
        if (m_scene.Save(m_currentScenePath)) {
            m_scene.ClearDirty();
            UpdateWindowTitle();
        }
    }

    void DoSaveSceneAs() {
        const std::string path = PickFileDialog(
            m_hwnd, CLSID_FileSaveDialog, L"Save Scene As",
            L"NaraGame Scene (*.nscene)", L"*.nscene", L"nscene",
            ScenesDir());
        if (path.empty()) return;
        if (m_scene.Save(path)) {
            m_currentScenePath = path;
            m_scene.ClearDirty();
            // Use the file stem as the scene name so the title and JSON
            // line up with what the user picked.
            const size_t slash = path.find_last_of('/');
            std::string stem = (slash == std::string::npos)
                ? path : path.substr(slash + 1);
            const size_t dot = stem.rfind('.');
            if (dot != std::string::npos) stem.resize(dot);
            m_scene.SetName(stem);
            UpdateWindowTitle();
        }
    }

    void DrawMainMenuBar() {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...", "Ctrl+Shift+N"))
                StartNewProjectFlow();
            if (ImGui::MenuItem("Open Project...", "Ctrl+Shift+O"))
                DoOpenProject();
            if (ImGui::MenuItem("Close Project", nullptr, false, m_project.IsOpen()))
                DoCloseProject();
            ImGui::Separator();
            const bool projOpen = m_project.IsOpen();
            if (ImGui::MenuItem("New Scene", "Ctrl+N", false, projOpen))
                DoNewScene();
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, projOpen))
                DoOpenScene();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, projOpen))
                DoSaveScene();
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, projOpen))
                DoSaveSceneAs();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) Close();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Reset Layout")) m_resetLayout = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("ImGui Demo", nullptr, &m_showDemo);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    void DrawNewProjectModal() {
        if (m_openNewProjectModal) {
            ImGui::OpenPopup("New Project");
            m_openNewProjectModal = false;
        }
        if (!ImGui::BeginPopupModal("New Project", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextUnformatted("Project name");
        ImGui::PushItemWidth(420.0f);
        ImGui::InputText("##name", m_newProjectName.data(),
                         m_newProjectName.size());
        ImGui::TextUnformatted("Location (parent folder)");
        ImGui::InputText("##path", m_newProjectPath.data(),
                         m_newProjectPath.size());
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Browse...##path")) {
            const std::string folder =
                PickFolderDialog(m_hwnd, L"Pick parent folder");
            if (!folder.empty()) {
                std::snprintf(m_newProjectPath.data(),
                              m_newProjectPath.size(),
                              "%s", folder.c_str());
            }
        }
        if (!m_newProjectError.empty()) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                               "%s", m_newProjectError.c_str());
        }
        const bool canCreate = std::strlen(m_newProjectName.data()) > 0 &&
                               std::strlen(m_newProjectPath.data()) > 0;
        ImGui::Separator();
        if (!canCreate) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            std::string parent = m_newProjectPath.data();
            for (char& c : parent) if (c == '\\') c = '/';
            if (!parent.empty() && parent.back() == '/') parent.pop_back();
            const std::string name = m_newProjectName.data();
            const std::string root = parent + "/" + name;
            SceneEditor::Project p;
            if (p.Create(root, name)) {
                m_project = std::move(p);
                m_assetDb.Open(m_project);
                UpdateWindowTitle();
                ImGui::CloseCurrentPopup();
            } else {
                m_newProjectError = "Could not create project at '" + root +
                                    "'. Folder must be empty.";
            }
        }
        if (!canCreate) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_newProjectError.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- Panels ----------------------------------------------------------

    void DrawHierarchyPanel() {
        ImGui::Begin("Hierarchy");
        if (!m_project.IsOpen()) {
            ImGui::TextDisabled("No project open.");
            ImGui::End();
            return;
        }
        if (ImGui::Button("+ Entity")) {
            const SceneEditor::EntityId id = m_scene.CreateEntity("Entity");
            m_selection = id;
            UpdateWindowTitle();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Scene: %s%s",
                            m_scene.Name().c_str(),
                            m_scene.IsDirty() ? "*" : "");
        ImGui::Separator();
        // Render each root, recurse into children. Right-click any node
        // opens a Delete / Duplicate(stub) context menu.
        for (SceneEditor::EntityId root : m_scene.Roots()) {
            DrawHierarchyNode(root);
        }
        // Click on empty area clears the selection so the inspector
        // shows the empty state again. The same blank area doubles as a
        // drop target so a texture dragged here spawns a root entity.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y > 0) {
            ImGui::InvisibleButton("##hier_blank", ImVec2(-1, avail.y));
            if (ImGui::IsItemClicked()) m_selection = SceneEditor::kInvalidEntityId;
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p =
                        ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                    if (p->DataSize == sizeof(SceneEditor::AssetGuid)) {
                        SceneEditor::AssetGuid guid;
                        std::memcpy(&guid, p->Data, sizeof(guid));
                        // Spawn at world origin; user can re-position
                        // via inspector or by dragging in the viewport.
                        SpawnSpriteEntityAt(guid, ImVec2{0.0f, 0.0f});
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();
    }

    void DrawHierarchyNode(SceneEditor::EntityId id) {
        SceneEditor::Entity* e = m_scene.Get(id);
        if (!e) return;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (e->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf |
                                           ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (id == m_selection) flags |= ImGuiTreeNodeFlags_Selected;

        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(size_t(id)),
                                            flags, "%s", e->name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_selection     = id;
            m_selectedAsset = SceneEditor::AssetGuid{};
        }
        // Drop target: a texture dragged onto an entity row spawns the
        // new sprite as a child of this entity.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                if (p->DataSize == sizeof(SceneEditor::AssetGuid)) {
                    SceneEditor::AssetGuid guid;
                    std::memcpy(&guid, p->Data, sizeof(guid));
                    SpawnSpriteEntityAt(guid, ImVec2{0.0f, 0.0f}, id);
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Add Child")) {
                m_scene.CreateEntity("Entity", id);
                UpdateWindowTitle();
            }
            if (ImGui::MenuItem("Delete")) {
                m_scene.DestroyEntity(id);
                if (m_selection == id) m_selection = SceneEditor::kInvalidEntityId;
                UpdateWindowTitle();
                ImGui::EndPopup();
                return;
            }
            ImGui::EndPopup();
        }
        if (open && !e->children.empty()) {
            // Copy ids first so deletions in the loop body don't trip iterators.
            const std::vector<SceneEditor::EntityId> kids = e->children;
            for (SceneEditor::EntityId c : kids) DrawHierarchyNode(c);
            ImGui::TreePop();
        }
    }

    void DrawInspectorPanel() {
        ImGui::Begin("Inspector");
        if (!m_project.IsOpen()) {
            ImGui::TextDisabled("No project open.");
            ImGui::End();
            return;
        }
        SceneEditor::Entity* e = m_scene.Get(m_selection);
        if (!e) {
            // No entity selection -- fall through to the asset
            // inspector if one is selected in the Project panel.
            if (m_selectedAsset.IsValid()) {
                DrawAssetInspector();
            } else {
                ImGui::TextDisabled("No selection.");
            }
            ImGui::End();
            return;
        }
        // --- Header fields (name + enabled) -----------------------------
        char nameBuf[256];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", e->name.c_str());
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            e->name = nameBuf;
            m_scene.MarkDirty();
            UpdateWindowTitle();
        }
        if (ImGui::Checkbox("Enabled", &e->enabled)) {
            m_scene.MarkDirty();
            UpdateWindowTitle();
        }
        ImGui::TextDisabled("id %llu", e->id);
        ImGui::Separator();

        // --- Components -------------------------------------------------
        for (auto& comp : e->components) {
            DrawComponentEditor(*e, comp.get());
        }
        // Add Component button. Lists every registered factory we don't
        // already have on this entity (Transform always present, so it
        // never re-offers itself).
        if (ImGui::Button("Add Component...")) {
            ImGui::OpenPopup("##add_component");
        }
        if (ImGui::BeginPopup("##add_component")) {
            for (const std::string& type :
                 SceneEditor::ComponentRegistry::Get().TypeNames()) {
                const bool already = std::any_of(
                    e->components.begin(), e->components.end(),
                    [&](const auto& c) { return type == c->TypeName(); });
                if (already) continue;
                if (ImGui::MenuItem(type.c_str())) {
                    if (auto comp = SceneEditor::ComponentRegistry::Get()
                                        .Create(type)) {
                        e->components.push_back(std::move(comp));
                        m_scene.MarkDirty();
                        UpdateWindowTitle();
                    }
                }
            }
            ImGui::EndPopup();
        }
        ImGui::End();
    }

    void DrawComponentEditor(SceneEditor::Entity& entity,
                             SceneEditor::Component* comp) {
        if (!comp) return;
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
                                         ImGuiTreeNodeFlags_Framed;
        if (!ImGui::CollapsingHeader(comp->TypeName(), flags)) return;
        // Type-specific editor. New component types add a case here (or
        // grow a virtual DrawInspector once the surface stabilises).
        if (auto* t = dynamic_cast<SceneEditor::TransformComponent*>(comp)) {
            DrawTransformEditor(*t);
        } else if (auto* s = dynamic_cast<SceneEditor::SpriteRendererComponent*>(comp)) {
            DrawSpriteRendererEditor(entity, *s);
        } else {
            ImGui::TextDisabled("(no inspector yet for %s)", comp->TypeName());
        }
    }

    // Asset Inspector: shown when the user clicks a file in the Project
    // panel and no entity is selected. M2c3 ships the Texture variant;
    // other asset types fall through to a stub for now.
    void DrawAssetInspector() {
        SceneEditor::AssetEntry* entry =
            m_assetDb.FindByGuidMutable(m_selectedAsset);
        if (!entry) {
            ImGui::TextDisabled("Selected asset no longer exists.");
            return;
        }
        ImGui::Text("Asset:  %s", entry->relPath.c_str());
        ImGui::Text("Type:   %s", entry->typeName.c_str());
        ImGui::TextDisabled("guid %s", entry->guid.ToString().c_str());
        ImGui::Separator();
        if (entry->typeName == "Texture") {
            DrawTextureImportEditor(*entry);
        } else {
            ImGui::TextDisabled("(no import settings editor for %s yet)",
                                entry->typeName.c_str());
        }
    }

    void DrawTextureImportEditor(SceneEditor::AssetEntry& entry) {
        // Decode the entry's existing importSettings, edit through the
        // struct form, then write back on any change. Auto-save +
        // texture-evict so the viewport reflects the new settings in
        // the very next frame.
        SceneEditor::TextureImportSettings imp;
        imp.FromJson(entry.importSettings);
        const auto before = imp;

        ImGui::DragFloat("Pixels Per Unit", &imp.pixelsPerUnit, 0.5f,
                         1.0f, 4096.0f, "%.2f");
        const char* pivotNames[] = { "Center", "TopLeft", "TopRight",
                                     "BottomLeft", "BottomRight",
                                     "Custom" };
        int pivotIdx = int(imp.pivotMode);
        if (ImGui::Combo("Pivot", &pivotIdx, pivotNames, 6)) {
            imp.pivotMode = static_cast<
                SceneEditor::TextureImportSettings::PivotMode>(pivotIdx);
        }
        if (imp.pivotMode ==
            SceneEditor::TextureImportSettings::PivotMode::Custom) {
            float cp[2] = { imp.customPivot.x, imp.customPivot.y };
            if (ImGui::DragFloat2("Custom Pivot", cp, 0.01f, 0.0f, 1.0f)) {
                imp.customPivot = { cp[0], cp[1] };
            }
        }
        const char* filterNames[] = { "Linear", "Point" };
        int filterIdx = int(imp.filterMode);
        if (ImGui::Combo("Filter", &filterIdx, filterNames, 2)) {
            imp.filterMode = static_cast<
                SceneEditor::TextureImportSettings::Filter>(filterIdx);
        }
        const char* wrapNames[] = { "Clamp", "Repeat", "Mirror" };
        int wrapIdx = int(imp.wrapMode);
        if (ImGui::Combo("Wrap", &wrapIdx, wrapNames, 3)) {
            imp.wrapMode = static_cast<
                SceneEditor::TextureImportSettings::Wrap>(wrapIdx);
        }
        ImGui::Checkbox("Alpha is Transparency", &imp.alphaIsTransparency);

        const bool changed = std::memcmp(&imp, &before, sizeof(imp)) != 0;
        if (changed) {
            entry.importSettings = imp.ToJson();
            m_assetDb.SaveMeta(entry.guid);
            // FilterMode is sampler state baked into the texture object;
            // evict so the next GetTexture picks up the new setting.
            // PPU + pivot are read live by DrawSpriteEntity each frame,
            // no eviction needed.
            if (imp.filterMode != before.filterMode) {
                m_assetDb.EvictTexture(entry.guid);
            }
        }
    }

    void DrawTransformEditor(SceneEditor::TransformComponent& t) {
        float pos[3]   = { t.position.x, t.position.y, t.position.z };
        float scale[3] = { t.scale.x,    t.scale.y,    t.scale.z };
        bool changed = false;
        if (ImGui::DragFloat3("Position", pos, 0.05f)) {
            t.position = { pos[0], pos[1], pos[2] };
            changed = true;
        }
        if (ImGui::DragFloat("Rotation Z", &t.rotation, 0.5f,
                             -3600.0f, 3600.0f, "%.2f deg")) {
            changed = true;
        }
        if (ImGui::DragFloat3("Scale", scale, 0.01f, -1000.0f, 1000.0f)) {
            t.scale = { scale[0], scale[1], scale[2] };
            changed = true;
        }
        if (changed) {
            m_scene.MarkDirty();
            UpdateWindowTitle();
        }
    }

    void DrawSpriteRendererEditor(SceneEditor::Entity& entity,
                                  SceneEditor::SpriteRendererComponent& s) {
        bool changed = false;
        // Sprite picker -- dropdown over every Texture asset in the
        // project. Drag-and-drop from the Project panel is M2c3; this is
        // the keyboard/mouse fallback that ships now.
        std::string current = "(none)";
        if (s.sprite.IsValid()) {
            if (const auto* e = m_assetDb.FindByGuid(s.sprite)) {
                current = e->relPath;
            } else {
                current = s.sprite.ToString();
            }
        }
        if (ImGui::BeginCombo("Sprite", current.c_str())) {
            if (ImGui::Selectable("(none)", !s.sprite.IsValid())) {
                s.sprite = SceneEditor::AssetGuid{};
                changed = true;
            }
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Texture") continue;
                const bool sel = (a.guid == s.sprite);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    s.sprite = a.guid;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        // Drop a texture from the Project panel directly onto the combo
        // to swap the sprite without opening the dropdown.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                if (p->DataSize == sizeof(SceneEditor::AssetGuid)) {
                    SceneEditor::AssetGuid guid;
                    std::memcpy(&guid, p->Data, sizeof(guid));
                    if (const auto* e = m_assetDb.FindByGuid(guid)) {
                        if (e->typeName == "Texture") {
                            s.sprite = guid;
                            changed = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        float col[4] = { s.color.r, s.color.g, s.color.b, s.color.a };
        if (ImGui::ColorEdit4("Tint", col)) {
            s.color = { col[0], col[1], col[2], col[3] };
            changed = true;
        }
        changed |= ImGui::Checkbox("Flip X", &s.flipX);
        changed |= ImGui::Checkbox("Flip Y", &s.flipY);
        const char* modes[] = { "Simple", "Sliced", "Tiled" };
        int idx = int(s.drawMode);
        if (ImGui::Combo("Draw Mode", &idx, modes, 3)) {
            s.drawMode = static_cast<SceneEditor::SpriteDrawMode>(idx);
            changed = true;
        }
        if (s.drawMode != SceneEditor::SpriteDrawMode::Simple) {
            float sz[2] = { s.size.x, s.size.y };
            if (ImGui::DragFloat2("Size", sz, 0.05f, 0.0f, 1024.0f)) {
                s.size = { sz[0], sz[1] };
                changed = true;
            }
        }
        char layerBuf[64];
        std::snprintf(layerBuf, sizeof(layerBuf), "%s",
                      s.sortingLayer.c_str());
        if (ImGui::InputText("Sorting Layer", layerBuf, sizeof(layerBuf))) {
            s.sortingLayer = layerBuf;
            changed = true;
        }
        if (ImGui::DragInt("Order in Layer", &s.orderInLayer, 0.2f,
                           -1000, 1000)) {
            changed = true;
        }
        if (ImGui::SmallButton("Remove##sprite")) {
            // Lazy remove: find and erase by raw pointer comparison.
            for (auto it = entity.components.begin();
                 it != entity.components.end(); ++it) {
                if (it->get() == &s) { entity.components.erase(it); break; }
            }
            changed = true;
        }
        if (changed) {
            m_scene.MarkDirty();
            UpdateWindowTitle();
        }
    }

    // --- 2D viewport ------------------------------------------------------
    //
    // The viewport is drawn entirely with ImGui's DrawList for M1: we
    // capture an invisible button covering the panel for input, then
    // walk the scene + grid + axes into the same list. SpriteRenderer
    // arrives in M2 and at that point this path swaps to a real
    // Renderer2D + offscreen RT pipeline; entity markers below stand in
    // for the asset-driven visuals.
    struct ViewportCamera {
        ImVec2 center{0.0f, 0.0f};   // world coords at the panel centre
        float  zoom = 48.0f;         // pixels per world unit
    };

    // Translate gizmo state. None means we're not in a drag; the other
    // values are the constrained axis the drag locks onto. Both is the
    // free-move flavour used either from clicking the centre square or
    // from clicking the entity's body directly.
    enum class GizmoAxis { None, X, Y, Both };

    // Screen-space hit zones / draw constants for the gizmo. Stay in
    // pixels (not world units) so the handles are always the same size
    // no matter how far the user has zoomed.
    static constexpr float kGizmoHandleGap   = 12.0f;  // dead zone around centre
    static constexpr float kGizmoArrowLen    = 60.0f;  // arrow shaft length
    static constexpr float kGizmoArrowHead   = 12.0f;
    static constexpr float kGizmoArrowThick  = 8.0f;   // hit-test half-thickness
    static constexpr float kGizmoCenterHalf  = 9.0f;   // centre square half-size

    ImVec2 WorldToScreen(const ImVec2& worldPos,
                         const ImVec2& panelMin,
                         const ImVec2& panelSize) const {
        const ImVec2 centre{panelMin.x + panelSize.x * 0.5f,
                            panelMin.y + panelSize.y * 0.5f};
        return ImVec2{centre.x + (worldPos.x - m_viewCam.center.x) * m_viewCam.zoom,
                      centre.y - (worldPos.y - m_viewCam.center.y) * m_viewCam.zoom};
    }

    ImVec2 ScreenToWorld(const ImVec2& screenPos,
                         const ImVec2& panelMin,
                         const ImVec2& panelSize) const {
        const ImVec2 centre{panelMin.x + panelSize.x * 0.5f,
                            panelMin.y + panelSize.y * 0.5f};
        return ImVec2{m_viewCam.center.x + (screenPos.x - centre.x) / m_viewCam.zoom,
                      m_viewCam.center.y - (screenPos.y - centre.y) / m_viewCam.zoom};
    }

    // Compose the entity's world-space transform by walking up the
    // parent chain and summing local translations. Rotation + scale are
    // ignored for the marker pass; the goal is to show the user where
    // an entity is, not to fully simulate the transform stack.
    ImVec2 EntityWorldPos(const SceneEditor::Entity& e) const {
        ImVec2 acc{0.0f, 0.0f};
        const SceneEditor::Entity* cur = &e;
        int safety = 256;
        while (cur && safety-- > 0) {
            if (const auto* t = cur->GetComponent<SceneEditor::TransformComponent>()) {
                acc.x += t->position.x;
                acc.y += t->position.y;
            }
            cur = (cur->parent == SceneEditor::kInvalidEntityId)
                ? nullptr : m_scene.Get(cur->parent);
        }
        return acc;
    }

    void DrawViewportPanel() {
        ImGui::Begin("Viewport");

        // Toolbar row: zoom / camera controls at a glance.
        ImGui::Text("Zoom %.0f px/u  |  Centre (%.2f, %.2f)",
                    m_viewCam.zoom, m_viewCam.center.x, m_viewCam.center.y);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset View")) {
            m_viewCam = ViewportCamera{};
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Frame Selected") && m_selection) {
            if (const auto* e = m_scene.Get(m_selection)) {
                m_viewCam.center = EntityWorldPos(*e);
            }
        }

        const ImVec2 panelMin  = ImGui::GetCursorScreenPos();
        const ImVec2 panelSize = ImGui::GetContentRegionAvail();
        if (panelSize.x < 4 || panelSize.y < 4) {
            ImGui::End();
            return;
        }
        // Invisible button captures all mouse interaction over the
        // viewport area. ImGui still renders the DrawList commands we
        // queue after it because we draw to GetWindowDrawList().
        ImGui::InvisibleButton("##viewport_hit", panelSize,
                               ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();

        // Drop target: a texture from the Project panel lands as a new
        // entity at the cursor's world position with a SpriteRenderer
        // already pointing at the dropped asset.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                if (p->DataSize == sizeof(SceneEditor::AssetGuid)) {
                    SceneEditor::AssetGuid guid;
                    std::memcpy(&guid, p->Data, sizeof(guid));
                    const ImVec2 world = ScreenToWorld(ImGui::GetMousePos(),
                                                       panelMin, panelSize);
                    SpawnSpriteEntityAt(guid, world);
                }
            }
            ImGui::EndDragDropTarget();
        }

        HandleViewportInput(hovered, active, panelMin, panelSize);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Clip to the viewport region so grid lines and entity markers
        // can't bleed into the dockspace edges.
        dl->PushClipRect(panelMin,
                         ImVec2{panelMin.x + panelSize.x,
                                panelMin.y + panelSize.y}, true);
        // Background fill.
        dl->AddRectFilled(panelMin,
                          ImVec2{panelMin.x + panelSize.x,
                                 panelMin.y + panelSize.y},
                          IM_COL32(28, 30, 34, 255));

        DrawGrid(dl, panelMin, panelSize);
        DrawEntities(dl, panelMin, panelSize);
        DrawGizmo(dl, panelMin, panelSize);

        dl->PopClipRect();
        ImGui::End();
    }

    // Create a new entity with a SpriteRenderer pointing at `guid`,
    // placed at the given world coordinates. Non-texture / unknown
    // guids are silently ignored so a stray drop doesn't pollute the
    // scene with a placeholder. The new entity is auto-selected so
    // the Inspector reflects it right away.
    void SpawnSpriteEntityAt(const SceneEditor::AssetGuid& guid,
                             const ImVec2& worldPos,
                             SceneEditor::EntityId parent =
                                 SceneEditor::kInvalidEntityId) {
        if (!guid.IsValid()) return;
        const auto* entry = m_assetDb.FindByGuid(guid);
        if (!entry || entry->typeName != "Texture") return;
        // Use the file stem as a friendly entity name.
        std::string stem = entry->relPath;
        const size_t slash = stem.find_last_of('/');
        if (slash != std::string::npos) stem = stem.substr(slash + 1);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos) stem.resize(dot);

        const SceneEditor::EntityId id =
            m_scene.CreateEntity(stem.empty() ? "Sprite" : stem, parent);
        SceneEditor::Entity* e = m_scene.Get(id);
        if (!e) return;
        if (auto* t = e->GetComponent<SceneEditor::TransformComponent>()) {
            t->position = { worldPos.x, worldPos.y, 0.0f };
        }
        auto spr = std::make_unique<SceneEditor::SpriteRendererComponent>();
        spr->sprite = guid;
        e->components.push_back(std::move(spr));
        m_selection = id;
        UpdateWindowTitle();
    }

    // Which gizmo handle the given screen-space point lies on, if any.
    // Returns None whenever there's no selected entity (we draw nothing
    // in that case so there's nothing to hit either).
    GizmoAxis HitTestGizmo(const ImVec2& screenPos,
                           const ImVec2& panelMin,
                           const ImVec2& panelSize) const {
        if (m_selection == SceneEditor::kInvalidEntityId) return GizmoAxis::None;
        const auto* e = m_scene.Get(m_selection);
        if (!e) return GizmoAxis::None;
        const ImVec2 wp = EntityWorldPos(*e);
        const ImVec2 c  = WorldToScreen(wp, panelMin, panelSize);
        const float dx = screenPos.x - c.x;
        const float dy = screenPos.y - c.y;
        // Centre square first -- it overlaps the inner ends of the
        // arrows by design (drag from the centre = both axes).
        if (std::abs(dx) <= kGizmoCenterHalf && std::abs(dy) <= kGizmoCenterHalf)
            return GizmoAxis::Both;
        // +X arrow: extends to the right on screen (world +X).
        if (dx >= kGizmoHandleGap && dx <= kGizmoArrowLen &&
            std::abs(dy) <= kGizmoArrowThick)
            return GizmoAxis::X;
        // +Y arrow: extends upward on screen (world +Y, screen -Y).
        if (std::abs(dx) <= kGizmoArrowThick &&
            dy <= -kGizmoHandleGap && dy >= -kGizmoArrowLen)
            return GizmoAxis::Y;
        return GizmoAxis::None;
    }

    void DrawGizmo(ImDrawList* dl, const ImVec2& panelMin,
                   const ImVec2& panelSize) {
        if (m_selection == SceneEditor::kInvalidEntityId) return;
        const auto* e = m_scene.Get(m_selection);
        if (!e) return;
        const ImVec2 wp = EntityWorldPos(*e);
        const ImVec2 c  = WorldToScreen(wp, panelMin, panelSize);

        const GizmoAxis live = (m_gizmoActive != GizmoAxis::None)
                                 ? m_gizmoActive : m_gizmoHover;
        const ImU32 colX     = (live == GizmoAxis::X)
                                 ? IM_COL32(255, 120, 120, 255)
                                 : IM_COL32(210,  60,  60, 255);
        const ImU32 colY     = (live == GizmoAxis::Y)
                                 ? IM_COL32(120, 255, 140, 255)
                                 : IM_COL32( 60, 200,  90, 255);
        const ImU32 colBoth  = (live == GizmoAxis::Both)
                                 ? IM_COL32(255, 230, 100, 255)
                                 : IM_COL32(220, 200,  80, 255);

        // X arrow (red, right).
        const ImVec2 xTipBase{c.x + kGizmoArrowLen - kGizmoArrowHead, c.y};
        const ImVec2 xTip    {c.x + kGizmoArrowLen,                    c.y};
        dl->AddLine(ImVec2{c.x + kGizmoHandleGap, c.y}, xTipBase, colX, 3.0f);
        dl->AddTriangleFilled(xTip,
                              ImVec2{xTipBase.x, xTipBase.y - kGizmoArrowHead * 0.45f},
                              ImVec2{xTipBase.x, xTipBase.y + kGizmoArrowHead * 0.45f},
                              colX);

        // Y arrow (green, up on screen).
        const ImVec2 yTipBase{c.x, c.y - kGizmoArrowLen + kGizmoArrowHead};
        const ImVec2 yTip    {c.x, c.y - kGizmoArrowLen};
        dl->AddLine(ImVec2{c.x, c.y - kGizmoHandleGap}, yTipBase, colY, 3.0f);
        dl->AddTriangleFilled(yTip,
                              ImVec2{yTipBase.x - kGizmoArrowHead * 0.45f, yTipBase.y},
                              ImVec2{yTipBase.x + kGizmoArrowHead * 0.45f, yTipBase.y},
                              colY);

        // Centre handle.
        dl->AddRectFilled(ImVec2{c.x - kGizmoCenterHalf, c.y - kGizmoCenterHalf},
                          ImVec2{c.x + kGizmoCenterHalf, c.y + kGizmoCenterHalf},
                          colBoth, 2.0f);
        dl->AddRect(ImVec2{c.x - kGizmoCenterHalf, c.y - kGizmoCenterHalf},
                    ImVec2{c.x + kGizmoCenterHalf, c.y + kGizmoCenterHalf},
                    IM_COL32(30, 30, 30, 255), 2.0f, 0, 1.0f);
    }

    // True if the screen point overlaps any drawn entity (any of its
    // sprite quad bounds for sprite entities, the 0.6-unit marker box
    // for plain ones). Returns the topmost (last-drawn) hit so a child
    // that's painted over a parent wins.
    SceneEditor::EntityId HitTestEntity(const ImVec2& worldMouse) const {
        std::vector<SceneEditor::EntityId> ids;
        ids.reserve(64);
        CollectAllEntities(m_scene.Roots(), ids);
        for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
            const auto* e = m_scene.Get(*it);
            if (!e || !e->enabled) continue;
            const ImVec2 wp = EntityWorldPos(*e);
            if (std::abs(worldMouse.x - wp.x) <= 0.5f &&
                std::abs(worldMouse.y - wp.y) <= 0.5f) {
                return *it;
            }
        }
        return SceneEditor::kInvalidEntityId;
    }

    void HandleViewportInput(bool hovered, bool active,
                             const ImVec2& panelMin,
                             const ImVec2& panelSize) {
        ImGuiIO& io = ImGui::GetIO();

        // Wheel zoom around the cursor so the world point under the
        // mouse stays put. Skip zoom while a drag is in progress so a
        // fast pan with the wheel doesn't get hijacked.
        if (hovered && std::abs(io.MouseWheel) > 0.0f) {
            const ImVec2 worldBefore = ScreenToWorld(io.MousePos,
                                                     panelMin, panelSize);
            const float factor = std::pow(1.15f, io.MouseWheel);
            m_viewCam.zoom = std::clamp(m_viewCam.zoom * factor, 4.0f, 512.0f);
            const ImVec2 worldAfter = ScreenToWorld(io.MousePos,
                                                    panelMin, panelSize);
            m_viewCam.center.x += worldBefore.x - worldAfter.x;
            m_viewCam.center.y += worldBefore.y - worldAfter.y;
        }
        // Mid-button drag pans. We don't need ImGui::IsItemActive for the
        // middle button -- IsMouseDragging works against the active item.
        if ((active || hovered) && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            const ImVec2 d = io.MouseDelta;
            m_viewCam.center.x -= d.x / m_viewCam.zoom;
            m_viewCam.center.y += d.y / m_viewCam.zoom;  // screen Y inverted
        }

        // Hover feedback: highlight whichever gizmo handle the mouse is
        // over (only meaningful when we have a selection). Cleared on
        // leave so the gizmo dims back down.
        m_gizmoHover = hovered ? HitTestGizmo(io.MousePos, panelMin, panelSize)
                               : GizmoAxis::None;

        // Left mouse-down. Order of preference:
        //   1. Gizmo handle on the current selection -> start axis drag,
        //      selection unchanged.
        //   2. Entity body -> select + start free-drag in one motion.
        //   3. Empty -> clear selection, no drag.
        if (active && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 worldMouse = ScreenToWorld(io.MousePos,
                                                    panelMin, panelSize);
            const GizmoAxis gizHit = HitTestGizmo(io.MousePos, panelMin, panelSize);
            if (gizHit != GizmoAxis::None) {
                BeginDrag(gizHit, worldMouse);
            } else {
                const SceneEditor::EntityId entHit = HitTestEntity(worldMouse);
                if (entHit != SceneEditor::kInvalidEntityId) {
                    m_selection     = entHit;
                    m_selectedAsset = SceneEditor::AssetGuid{};
                    BeginDrag(GizmoAxis::Both, worldMouse);
                } else {
                    m_selection    = SceneEditor::kInvalidEntityId;
                    m_gizmoActive  = GizmoAxis::None;
                }
            }
        }
        // Drag in progress: apply the constrained delta to the entity's
        // local position. Ignore tiny mouse-down releases that didn't
        // actually move (no-op + no dirty flip).
        if (m_gizmoActive != GizmoAxis::None &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 worldMouse = ScreenToWorld(io.MousePos,
                                                    panelMin, panelSize);
            ApplyGizmoDelta(worldMouse);
        }
        if (m_gizmoActive != GizmoAxis::None &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_gizmoActive = GizmoAxis::None;
            UpdateWindowTitle();
        }

        // 'F' key frames the selected entity.
        if (hovered && ImGui::IsKeyPressed(ImGuiKey_F) && m_selection) {
            if (const auto* e = m_scene.Get(m_selection)) {
                m_viewCam.center = EntityWorldPos(*e);
            }
        }
    }

    void BeginDrag(GizmoAxis axis, const ImVec2& worldMouse) {
        m_gizmoActive    = axis;
        m_gizmoDownWorld = worldMouse;
        if (auto* e = m_scene.Get(m_selection)) {
            if (auto* t = e->GetComponent<SceneEditor::TransformComponent>()) {
                m_gizmoStartPos = t->position;
            }
        }
    }

    void ApplyGizmoDelta(const ImVec2& worldMouse) {
        auto* e = m_scene.Get(m_selection);
        if (!e) return;
        auto* t = e->GetComponent<SceneEditor::TransformComponent>();
        if (!t) return;
        // Without parent rotation/scale (the current TransformComponent
        // semantics), world-space delta and local-space delta line up.
        // Z stays put -- this gizmo is the 2D translator.
        float dx = worldMouse.x - m_gizmoDownWorld.x;
        float dy = worldMouse.y - m_gizmoDownWorld.y;
        if (m_gizmoActive == GizmoAxis::X) dy = 0.0f;
        if (m_gizmoActive == GizmoAxis::Y) dx = 0.0f;
        const float newX = m_gizmoStartPos.x + dx;
        const float newY = m_gizmoStartPos.y + dy;
        if (newX != t->position.x || newY != t->position.y) {
            t->position.x = newX;
            t->position.y = newY;
            m_scene.MarkDirty();
        }
    }

    void CollectAllEntities(const std::vector<SceneEditor::EntityId>& roots,
                            std::vector<SceneEditor::EntityId>& out) const {
        for (SceneEditor::EntityId id : roots) {
            const auto* e = m_scene.Get(id);
            if (!e) continue;
            out.push_back(id);
            CollectAllEntities(e->children, out);
        }
    }

    void DrawGrid(ImDrawList* dl, const ImVec2& panelMin,
                  const ImVec2& panelSize) const {
        const ImVec2 worldTL = ScreenToWorld(panelMin, panelMin, panelSize);
        const ImVec2 worldBR = ScreenToWorld(
            ImVec2{panelMin.x + panelSize.x, panelMin.y + panelSize.y},
            panelMin, panelSize);
        // Skip drawing every cell line at low zoom -- choose a step that
        // keeps the on-screen line spacing >= 6 px.
        int step = 1;
        while (step * m_viewCam.zoom < 6.0f) step *= 5;
        const int xMin = int(std::floor(std::min(worldTL.x, worldBR.x) / step)) * step;
        const int xMax = int(std::ceil (std::max(worldTL.x, worldBR.x) / step)) * step;
        const int yMin = int(std::floor(std::min(worldTL.y, worldBR.y) / step)) * step;
        const int yMax = int(std::ceil (std::max(worldTL.y, worldBR.y) / step)) * step;
        const ImU32 minorCol = IM_COL32(60, 62, 68, 255);
        const ImU32 majorCol = IM_COL32(80, 84, 92, 255);
        for (int x = xMin; x <= xMax; x += step) {
            const ImVec2 a = WorldToScreen(ImVec2(float(x), float(yMin)),
                                            panelMin, panelSize);
            const ImVec2 b = WorldToScreen(ImVec2(float(x), float(yMax)),
                                            panelMin, panelSize);
            const ImU32 col = (x % (step * 5) == 0) ? majorCol : minorCol;
            dl->AddLine(a, b, col, 1.0f);
        }
        for (int y = yMin; y <= yMax; y += step) {
            const ImVec2 a = WorldToScreen(ImVec2(float(xMin), float(y)),
                                            panelMin, panelSize);
            const ImVec2 b = WorldToScreen(ImVec2(float(xMax), float(y)),
                                            panelMin, panelSize);
            const ImU32 col = (y % (step * 5) == 0) ? majorCol : minorCol;
            dl->AddLine(a, b, col, 1.0f);
        }
        // Axes: red X (worldY = 0) horizontal, green Y (worldX = 0) vertical.
        const ImVec2 xAxisA = WorldToScreen(ImVec2(float(xMin), 0.0f),
                                             panelMin, panelSize);
        const ImVec2 xAxisB = WorldToScreen(ImVec2(float(xMax), 0.0f),
                                             panelMin, panelSize);
        dl->AddLine(xAxisA, xAxisB, IM_COL32(200, 80, 80, 255), 1.5f);
        const ImVec2 yAxisA = WorldToScreen(ImVec2(0.0f, float(yMin)),
                                             panelMin, panelSize);
        const ImVec2 yAxisB = WorldToScreen(ImVec2(0.0f, float(yMax)),
                                             panelMin, panelSize);
        dl->AddLine(yAxisA, yAxisB, IM_COL32(80, 200, 100, 255), 1.5f);
    }

    void DrawEntities(ImDrawList* dl, const ImVec2& panelMin,
                      const ImVec2& panelSize) {
        std::vector<SceneEditor::EntityId> ids;
        CollectAllEntities(m_scene.Roots(), ids);
        for (SceneEditor::EntityId id : ids) {
            const auto* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            const auto* spr = e->GetComponent<SceneEditor::SpriteRendererComponent>();
            const ImVec2 wp = EntityWorldPos(*e);
            const ImVec2 c  = WorldToScreen(wp, panelMin, panelSize);
            const bool sel = (id == m_selection);
            if (spr && spr->sprite.IsValid()) {
                DrawSpriteEntity(dl, *e, *spr, c, sel);
            } else {
                DrawMarkerEntity(dl, *e, c, sel);
            }
        }
    }

    void DrawMarkerEntity(ImDrawList* dl, const SceneEditor::Entity& e,
                          const ImVec2& c, bool selected) const {
        const float half = 0.3f * m_viewCam.zoom;
        const ImVec2 a{c.x - half, c.y - half};
        const ImVec2 b{c.x + half, c.y + half};
        dl->AddRectFilled(a, b,
                          selected ? IM_COL32(220, 180, 70, 255)
                                   : IM_COL32(110, 160, 220, 255),
                          4.0f);
        dl->AddRect(a, b,
                    selected ? IM_COL32(255, 255, 200, 255)
                             : IM_COL32(40, 40, 50, 255),
                    4.0f, 0, selected ? 2.0f : 1.0f);
        dl->AddCircleFilled(c, 2.0f, IM_COL32(20, 20, 20, 255));
        const float ts = ImGui::GetFontSize();
        dl->AddText(ImVec2{c.x + half + 4.0f, c.y - ts * 0.5f},
                    IM_COL32(220, 220, 220, 255),
                    e.name.c_str());
    }

    void DrawSpriteEntity(ImDrawList* dl,
                          const SceneEditor::Entity& e,
                          const SceneEditor::SpriteRendererComponent& spr,
                          const ImVec2& c, bool selected) {
        // Lazy load through the asset DB. If the texture isn't there
        // yet -- or the file failed to decode -- fall back to the
        // marker so the user still sees the entity.
        NaraGame::RHITexture* tex =
            m_assetDb.GetTexture(GetDevice(), spr.sprite);
        if (!tex) {
            DrawMarkerEntity(dl, e, c, selected);
            return;
        }
        // Resolve the asset's import settings (pixels-per-unit + pivot)
        // so the quad reflects whatever the user picked in the asset
        // inspector. Defaults are 100 px/u + centred pivot, matching
        // the previous hard-coded values.
        SceneEditor::TextureImportSettings imp;
        if (const auto* entry = m_assetDb.FindByGuid(spr.sprite)) {
            imp.FromJson(entry->importSettings);
        }
        const float wWorld = float(tex->Width())  / imp.pixelsPerUnit;
        const float hWorld = float(tex->Height()) / imp.pixelsPerUnit;
        const NaraGame::Vec2 pivot = imp.ResolvedPivot();
        // World-space rect anchored on the entity's transform. The pivot
        // chooses which texel sits on the transform position: (0.5,0.5)
        // centres it, (0,0) puts the bottom-left of the sprite there,
        // and so on.
        const float worldLeft   = -pivot.x        * wWorld;
        const float worldRight  = (1.0f - pivot.x) * wWorld;
        const float worldBottom = -pivot.y        * hWorld;
        const float worldTop    = (1.0f - pivot.y) * hWorld;
        // Convert to screen pixels (Y-up world -> Y-down screen so
        // worldTop maps to the smaller screen Y).
        const ImVec2 a{c.x + worldLeft  * m_viewCam.zoom,
                       c.y - worldTop   * m_viewCam.zoom};
        const ImVec2 b{c.x + worldRight * m_viewCam.zoom,
                       c.y - worldBottom* m_viewCam.zoom};

        // UV flip via swapped corners. Sprite tint multiplies the texture.
        const ImVec2 uv0{spr.flipX ? 1.0f : 0.0f,
                         spr.flipY ? 1.0f : 0.0f};
        const ImVec2 uv1{spr.flipX ? 0.0f : 1.0f,
                         spr.flipY ? 0.0f : 1.0f};
        const ImU32 tint = IM_COL32(
            uint8_t(std::clamp(spr.color.r, 0.0f, 1.0f) * 255.0f),
            uint8_t(std::clamp(spr.color.g, 0.0f, 1.0f) * 255.0f),
            uint8_t(std::clamp(spr.color.b, 0.0f, 1.0f) * 255.0f),
            uint8_t(std::clamp(spr.color.a, 0.0f, 1.0f) * 255.0f));
        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));
        dl->AddImage(texId, a, b, uv0, uv1, tint);

        // Selection outline + label on top.
        if (selected) {
            dl->AddRect(a, b, IM_COL32(255, 220, 60, 255), 0.0f, 0, 2.0f);
        }
        const float ts = ImGui::GetFontSize();
        dl->AddText(ImVec2{a.x, b.y + 2.0f},
                    IM_COL32(220, 220, 220, 255),
                    e.name.c_str());
    }

    void DrawConsolePanel() {
        ImGui::Begin("Console");
        ImGui::TextWrapped("Editor + script log output will land here.");
        ImGui::Separator();
        ImGui::TextDisabled("(placeholder, M1)");
        ImGui::End();
    }

    void DrawProjectPanel() {
        ImGui::Begin("Project");
        if (!m_project.IsOpen()) {
            ImGui::TextDisabled("No project open.");
            ImGui::Separator();
            ImGui::TextWrapped("Use File > New Project... or Open "
                               "Project... to start.");
            if (ImGui::Button("New Project..."))  StartNewProjectFlow();
            ImGui::SameLine();
            if (ImGui::Button("Open Project...")) DoOpenProject();
            ImGui::End();
            return;
        }
        if (ImGui::Button("Refresh")) m_assetDb.Rescan();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu assets in '%s'",
                            m_assetDb.Entries().size(),
                            m_project.Name().c_str());
        ImGui::Separator();
        if (const SceneEditor::AssetTreeNode* root = m_assetDb.RootNode()) {
            DrawAssetTreeNode(*root, /*topLevel=*/true);
        }
        ImGui::End();
    }

    void DrawAssetTreeNode(const SceneEditor::AssetTreeNode& node,
                           bool topLevel) {
        if (node.isFolder) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                       ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (topLevel) flags |= ImGuiTreeNodeFlags_DefaultOpen;
            const bool open = ImGui::TreeNodeEx(node.name.c_str(), flags,
                                                "%s/", node.name.c_str());
            if (open) {
                for (const auto& c : node.children) DrawAssetTreeNode(*c, false);
                ImGui::TreePop();
            }
            return;
        }
        // File: show name + the classified type label, selectable +
        // drag source. Drop targets (Viewport / Inspector sprite slot /
        // Hierarchy blank area) accept the same "ASSET_GUID" payload.
        const SceneEditor::AssetEntry* e =
            (node.entryIndex < m_assetDb.Entries().size())
                ? &m_assetDb.Entries()[node.entryIndex]
                : nullptr;
        const bool selected = (e && m_selectedAsset == e->guid);
        ImGui::TreeNodeEx(node.name.c_str(),
                          ImGuiTreeNodeFlags_Leaf |
                          ImGuiTreeNodeFlags_NoTreePushOnOpen |
                          ImGuiTreeNodeFlags_SpanAvailWidth |
                          (selected ? ImGuiTreeNodeFlags_Selected : 0),
                          "%s", node.name.c_str());
        if (ImGui::IsItemClicked() && e) {
            m_selectedAsset = e->guid;
            m_selection     = SceneEditor::kInvalidEntityId;
        }
        if (e && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            // The 16-byte GUID is the entire payload. Editor drop sites
            // use the type tag to pick what to do with it.
            ImGui::SetDragDropPayload("ASSET_GUID", &e->guid, sizeof(e->guid));
            ImGui::Text("%s", node.name.c_str());
            ImGui::TextDisabled("[%s]", e->typeName.c_str());
            ImGui::EndDragDropSource();
        }
        if (e) {
            ImGui::SameLine();
            ImGui::TextDisabled("  [%s]", e->typeName.c_str());
            if (ImGui::IsItemHovered() ||
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("guid %s\npath  Assets/%s",
                                  e->guid.ToString().c_str(),
                                  e->relPath.c_str());
            }
        }
    }

    // --- Dockspace layout ------------------------------------------------

    void EnsureDefaultLayout(ImGuiID dockId) {
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId);
        const bool empty = (node == nullptr) || (node->IsLeafNode() &&
                                                 node->ChildNodes[0] == nullptr);
        if (!m_resetLayout && !empty) return;
        m_resetLayout = false;

        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId,
                                      ImGui::GetMainViewport()->WorkSize);

        ImGuiID centre = dockId;
        const ImGuiID rightId =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.22f,
                                        nullptr, &centre);
        const ImGuiID leftId =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.18f,
                                        nullptr, &centre);
        const ImGuiID bottomId =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.28f,
                                        nullptr, &centre);

        ImGui::DockBuilderDockWindow("Viewport",  centre);
        ImGui::DockBuilderDockWindow("Hierarchy", leftId);
        ImGui::DockBuilderDockWindow("Inspector", rightId);
        ImGui::DockBuilderDockWindow("Project",   bottomId);
        ImGui::DockBuilderDockWindow("Console",   bottomId);
        ImGui::DockBuilderFinish(dockId);
    }

    ImGuiContext*       m_imguiContext = nullptr;
    HWND                m_hwnd         = nullptr;
    bool                m_showDemo     = false;
    bool                m_resetLayout  = false;
    SceneEditor::Project m_project;

    // Active scene + path it last saved to / loaded from. Empty path
    // means an unsaved (untitled) scene, so the Save command falls
    // through to Save-As.
    SceneEditor::Scene    m_scene;
    std::string           m_currentScenePath;
    SceneEditor::EntityId m_selection = SceneEditor::kInvalidEntityId;

    SceneEditor::AssetDatabase m_assetDb;
    SceneEditor::AssetGuid     m_selectedAsset;

    ViewportCamera             m_viewCam;

    // Translate-gizmo state. Hover updates every hovered frame; active
    // sticks from mouse-down to release. m_gizmoDownWorld + m_gizmoStartPos
    // are the anchors the per-frame delta is applied against.
    GizmoAxis      m_gizmoHover     = GizmoAxis::None;
    GizmoAxis      m_gizmoActive    = GizmoAxis::None;
    ImVec2         m_gizmoDownWorld{0.0f, 0.0f};
    NaraGame::Vec3 m_gizmoStartPos{0.0f, 0.0f, 0.0f};

    // New Project modal state.
    bool                m_openNewProjectModal = false;
    std::array<char, 256> m_newProjectName{};
    std::array<char, 1024> m_newProjectPath{};
    std::string         m_newProjectError;

    std::string m_startupProjectPath;
    std::string m_startupScenePath;
};

} // namespace

Application* NaraGame::CreateApplication(int argc, char** argv) {
    std::string project, scene;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--project") project = argv[++i];
        else if (a == "--scene") scene = argv[++i];
    }
    return new EditorApp(std::move(project), std::move(scene));
}
