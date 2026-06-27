// scene-editor -- Unity-style scene editor shell. Runs on top of the
// NaraGame Application loop: the host drives the frame, ImGui's Win32
// backend gets WM_* events via Window::SetMessageHook, and ImGui's
// D3D11 backend draws on top of the engine's backbuffer. M1 commit 4
// adds Entity / Transform / Scene save+load + Hierarchy / Inspector
// panels on top of the project shell.

#include "asset/AssetDatabase.h"
#include "project/Project.h"
#include "scene/Component.h"
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "scene/TransformComponent.h"

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

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

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
        // shows the empty state again.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y > 0) {
            ImGui::InvisibleButton("##hier_blank", ImVec2(-1, avail.y));
            if (ImGui::IsItemClicked()) m_selection = SceneEditor::kInvalidEntityId;
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
            m_selection = id;
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
            ImGui::TextDisabled("No entity selected.");
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
            DrawComponentEditor(comp.get());
        }
        ImGui::End();
    }

    void DrawComponentEditor(SceneEditor::Component* comp) {
        if (!comp) return;
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
                                         ImGuiTreeNodeFlags_Framed;
        if (!ImGui::CollapsingHeader(comp->TypeName(), flags)) return;
        // Type-specific editor. New component types add a case here (or
        // grow a virtual DrawInspector once the surface stabilises).
        if (auto* t = dynamic_cast<SceneEditor::TransformComponent*>(comp)) {
            float pos[3]   = { t->position.x, t->position.y, t->position.z };
            float scale[3] = { t->scale.x,    t->scale.y,    t->scale.z };
            bool changed = false;
            if (ImGui::DragFloat3("Position", pos, 0.05f)) {
                t->position = { pos[0], pos[1], pos[2] };
                changed = true;
            }
            if (ImGui::DragFloat("Rotation Z", &t->rotation, 0.5f, -3600.0f, 3600.0f, "%.2f deg")) {
                changed = true;
            }
            if (ImGui::DragFloat3("Scale", scale, 0.01f, -1000.0f, 1000.0f)) {
                t->scale = { scale[0], scale[1], scale[2] };
                changed = true;
            }
            if (changed) {
                m_scene.MarkDirty();
                UpdateWindowTitle();
            }
        } else {
            ImGui::TextDisabled("(no inspector yet for %s)", comp->TypeName());
        }
    }

    void DrawViewportPanel() {
        ImGui::Begin("Viewport");
        ImGui::TextWrapped("2D ortho viewport + grid will render into an "
                           "offscreen RT and display here. (3D toggle is "
                           "a follow-up milestone.)");
        ImGui::Separator();
        ImGui::TextDisabled("(placeholder, M1)");
        ImGui::End();
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
        // File: show name + the classified type label, selectable for
        // future drag-drop / inspector wiring. Hover tooltip shows the
        // stable GUID for debugging asset references.
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
        if (ImGui::IsItemClicked() && e) m_selectedAsset = e->guid;
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
