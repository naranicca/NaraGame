// scene-editor -- Unity-style scene editor shell. Runs on top of the
// NaraGame Application loop: the host drives the frame, ImGui's Win32
// backend gets WM_* events via Window::SetMessageHook, and ImGui's
// D3D11 backend draws on top of the engine's backbuffer. M1 commit 4
// adds Entity / Transform / Scene save+load + Hierarchy / Inspector
// panels on top of the project shell.

#include "asset/AssetDatabase.h"
#include "asset/Animation.h"
#include "asset/AnimatorController.h"
#include "asset/Autotile.h"
#include "asset/Pak.h"
#include "asset/SpriteSheet.h"
#include "asset/TextureImportSettings.h"
#include "asset/Tileset.h"
#include "project/Project.h"
#include "scene/AnimatorComponent.h"
#include "scene/BoxColliderComponent.h"
#include "audio/Audio.h"
#include "scene/AudioListenerComponent.h"
#include "scene/AudioSourceComponent.h"
#include "scene/CapsuleColliderComponent.h"
#include "scene/CircleColliderComponent.h"
#include "scene/CameraComponent.h"
#include "scene/EdgeColliderComponent.h"
#include "scene/ParticleSystemComponent.h"
#include "scene/PolygonColliderComponent.h"
#include "scene/Component.h"
#include "scene/Entity.h"
#include "scene/Rigidbody2DComponent.h"
#include "scene/Scene.h"
#include "scene/ScriptComponent.h"
#include "scene/LuaComponent.h"
#include "scene/SplashScreenComponent.h"
#include "scene/SpriteRendererComponent.h"
#include "scene/TilemapComponent.h"
#include "scene/TransformComponent.h"
#include "scripting/ScriptHost.h"

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
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
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

// OS-default UI font search paths. First existing file in each list
// wins. `Latin` handles ASCII + European glyphs at 16 px; `Cjk` is
// merged on top and expected to cover Korean (GetGlyphRangesKorean).
// Missing files silently skip -- if neither list has a hit we fall
// back to ImGui's built-in ProggyClean bitmap font. Keeping this a
// candidate list rather than a hard-coded path avoids taking on
// fontconfig on Linux while still working across common distros.
#if defined(_WIN32)
constexpr const char* kEditorLatinFontCandidates[] = {
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/tahoma.ttf",
    "C:/Windows/Fonts/verdana.ttf",
    "C:/Windows/Fonts/arial.ttf",
    nullptr,
};
constexpr const char* kEditorCjkFontCandidates[] = {
    "C:/Windows/Fonts/malgun.ttf",   // Malgun Gothic (Korean)
    "C:/Windows/Fonts/msyh.ttc",     // Microsoft YaHei (broad CJK)
    nullptr,
};
#elif defined(__APPLE__)
constexpr const char* kEditorLatinFontCandidates[] = {
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/HelveticaNeue.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    nullptr,
};
constexpr const char* kEditorCjkFontCandidates[] = {
    "/System/Library/Fonts/AppleSDGothicNeo.ttc",
    "/System/Library/Fonts/PingFang.ttc",
    nullptr,
};
#else
constexpr const char* kEditorLatinFontCandidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    nullptr,
};
constexpr const char* kEditorCjkFontCandidates[] = {
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansKR-Regular.otf",
    "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
    nullptr,
};
#endif

const char* FirstExistingFontFile(const char* const* candidates) {
    std::error_code ec;
    for (const char* const* p = candidates; *p; ++p) {
        if (std::filesystem::exists(*p, ec)) return *p;
    }
    return nullptr;
}

// System UI scale factor (1.0 at 96 DPI). Windows 10 1607+ has
// GetDpiForWindow; other platforms fall back to 1.0 for now.
float DetectUiScale(void* nativeHandle) {
#if defined(_WIN32)
    HWND hwnd = reinterpret_cast<HWND>(nativeHandle);
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    return float(dpi) / 96.0f;
#else
    (void)nativeHandle;
    return 1.0f;
#endif
}

// Persist editor-wide UI preferences (uiScale + theme) in a tiny JSON
// file next to the ImGui .ini. Not tied to any project so the same
// laptop uses the same look regardless of which game the user is
// editing. Silent no-op on I/O failure.
constexpr const char* kEditorPrefsPath = "editor.prefs.json";

enum class EditorTheme { Dark, Light };

const char* EditorThemeToStr(EditorTheme t) {
    return (t == EditorTheme::Light) ? "light" : "dark";
}

EditorTheme EditorThemeFromStr(const std::string& s) {
    return (s == "light") ? EditorTheme::Light : EditorTheme::Dark;
}

// Rebuild the ImGui style from scratch: clear whatever previous
// ScaleAllSizes calls have compounded, drop the requested palette on
// top, then rescale to the current UI scale. Runs on start, on every
// preferences change, and inside the font-atlas rebuild path.
void ApplyEditorTheme(EditorTheme theme, float uiScale) {
    ImGui::GetStyle() = ImGuiStyle();
    switch (theme) {
        case EditorTheme::Light: ImGui::StyleColorsLight(); break;
        case EditorTheme::Dark:
        default:                 ImGui::StyleColorsDark();  break;
    }
    ImGui::GetStyle().ScaleAllSizes(uiScale);
}

struct EditorPrefs {
    float       uiScale = 1.0f;
    EditorTheme theme   = EditorTheme::Dark;
};

EditorPrefs LoadEditorPrefs(float scaleFallback) {
    EditorPrefs out;
    out.uiScale = scaleFallback;
    out.theme   = EditorTheme::Dark;

    std::ifstream in(kEditorPrefsPath, std::ios::binary);
    if (!in) return out;
    std::ostringstream ss; ss << in.rdbuf();
    std::string err;
    const auto root = NaraGame::MiniJson::Parse(ss.str(), &err);
    if (!root.IsObject()) return out;

    for (size_t i = 0; i < root.objectKeys.size(); ++i) {
        const std::string& k = root.objectKeys[i];
        const auto& v = root.objectValues[i];
        if (k == "uiScale" && v.IsNumber()) {
            float x = float(v.numberValue);
            if (x < 0.5f) x = 0.5f;
            if (x > 4.0f) x = 4.0f;
            out.uiScale = x;
        } else if (k == "theme" && v.IsString()) {
            out.theme = EditorThemeFromStr(v.stringValue);
        }
    }
    return out;
}

void SaveEditorPrefs(const EditorPrefs& p) {
    NaraGame::MiniJson::Value root = NaraGame::MiniJson::Value::MakeObject();
    root.Add("uiScale",
             NaraGame::MiniJson::Value::MakeNumber(p.uiScale));
    root.Add("theme",
             NaraGame::MiniJson::Value::MakeString(EditorThemeToStr(p.theme)));
    const std::string text = NaraGame::MiniJson::Write(root);
    std::ofstream out(kEditorPrefsPath, std::ios::binary);
    if (out) out << text;
}

// Load the editor UI font stack. Latin base + CJK merge. Sized around
// 16 px * uiScale so 4K monitors don't render the whole editor at
// postage-stamp size. Called from OnStart before the first ImGui
// frame; the D3D11 backend uploads the atlas on demand.
void LoadEditorFonts(ImGuiIO& io, float uiScale) {
    io.Fonts->Clear();
    const float baseSize = 16.0f * uiScale;

    ImFontConfig latinCfg;
    latinCfg.OversampleH = 3;
    latinCfg.OversampleV = 1;
    latinCfg.PixelSnapH  = false;

    ImFont* latin = nullptr;
    if (const char* latinPath = FirstExistingFontFile(kEditorLatinFontCandidates)) {
        latin = io.Fonts->AddFontFromFileTTF(latinPath, baseSize, &latinCfg);
        NARAGAME_INFO("scene-editor: loaded UI font '%s' @ %.1f px",
                      latinPath, baseSize);
    }

    if (latin) {
        if (const char* cjkPath = FirstExistingFontFile(kEditorCjkFontCandidates)) {
            ImFontConfig mergeCfg;
            mergeCfg.MergeMode   = true;
            mergeCfg.OversampleH = 2;
            mergeCfg.OversampleV = 1;
            mergeCfg.PixelSnapH  = false;
            io.Fonts->AddFontFromFileTTF(cjkPath, baseSize, &mergeCfg,
                                         io.Fonts->GetGlyphRangesKorean());
            NARAGAME_INFO("scene-editor: merged CJK font '%s'", cjkPath);
        }
    } else {
        NARAGAME_WARN("scene-editor: no OS UI font found; using ImGui default");
        io.Fonts->AddFontDefault();
    }
}

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
    EditorApp(std::string startupProject, std::string startupScene,
              bool standalone)
        : Application(MakeConfig()),
          m_startupProjectPath(std::move(startupProject)),
          m_startupScenePath(std::move(startupScene)),
          m_standalone(standalone) {}

    void OnStart() override {
        // COM apartment for IFileOpenDialog. ole32 is already linked
        // by the engine for XAudio2 / D3D11 init paths.
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        // Bind the live scene so the Lua `animator` API can resolve
        // entities by name. The pointer stays valid for the
        // app's lifetime -- scene swaps reuse the same Scene
        // instance + just rebuild its contents.
        m_scriptHost.BindScene(&m_scene);
        m_scriptHost.BindAssetDatabase(&m_assetDb);

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

        // OS-default UI font stack. Latin + CJK merged, scaled to the
        // system DPI so per-pixel work at 4K looks the same as at 1080p.
        // Must run before the DX11 backend init because the backend
        // uploads the font atlas texture the first time it sees one.
        // User-saved scale + theme in editor.prefs.json override the
        // DPI/dark defaults.
        const EditorPrefs prefs = LoadEditorPrefs(DetectUiScale(hwnd));
        m_uiScale = prefs.uiScale;
        m_theme   = prefs.theme;
        LoadEditorFonts(io, m_uiScale);
        ApplyEditorTheme(m_theme, m_uiScale);

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
                ApplyProjectAudioMixer();
                ReloadAllScripts();
                UpdateWindowTitle();
            }
        }
        NotifyAllScriptsForDestroy();
        if (!m_startupScenePath.empty() && m_scene.Load(m_startupScenePath)) {
            m_currentScenePath = m_startupScenePath;
            UpdateWindowTitle();
        }
        // M9c1 standalone: force Play on after the scene is loaded
        // so the user gets a running game immediately. The same
        // tick-physics / tick-scripts / audio paths the editor
        // uses fire from here on; the only difference is the
        // viewport-only OnRender path below.
        if (m_standalone) m_animPlaying = true;
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

        // Live font rebuild triggered by the Editor Preferences slider.
        // ImGui doesn't hot-swap the atlas on its own -- we tear down
        // the DX11 backend's font texture, rebuild the atlas at the new
        // scale, reset the style so ScaleAllSizes doesn't accumulate,
        // then let the backend re-upload on the next NewFrame. All of
        // this must happen BEFORE ImGui_ImplDX11_NewFrame or the frame
        // draws with the stale font. A theme-only change reuses the
        // style reset without touching the atlas.
        if (m_pendingFontRebuild) {
            ImGui_ImplDX11_InvalidateDeviceObjects();
            LoadEditorFonts(ImGui::GetIO(), m_uiScale);
            ApplyEditorTheme(m_theme, m_uiScale);
            ImGui_ImplDX11_CreateDeviceObjects();
            m_pendingFontRebuild  = false;
            m_pendingThemeRefresh = false;
        } else if (m_pendingThemeRefresh) {
            ApplyEditorTheme(m_theme, m_uiScale);
            m_pendingThemeRefresh = false;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // M9c1 standalone runtime path. No dockspace, no menu, no
        // editor chrome -- just the viewport at full window size +
        // the simulation ticks the editor's Play mode runs. Modals
        // (Sorting Layers, Audio Mixer, etc.) are also skipped
        // since none of them belong in a shipped game build.
        if (m_standalone) {
            TickScripts (ImGui::GetIO().DeltaTime);
            ConsumePendingSceneLoad();
            TickPhysics (ImGui::GetIO().DeltaTime);
            TickAnimators(ImGui::GetIO().DeltaTime);
            TickAudioSources(ImGui::GetIO().DeltaTime);
            TickParticles(ImGui::GetIO().DeltaTime);
            TickSplash(ImGui::GetIO().DeltaTime);
            SceneEditor::Audio::UpdateMusic();
            // M9c11 standalone camera drive. Re-resolve every
            // frame so a script that moves the camera entity
            // (or swaps priorities mid-cutscene) is visible
            // immediately. No camera component? Keep whatever
            // m_viewCam already holds.
            if (SceneEditor::Entity* camEnt =
                    ResolveActiveCameraEntity()) {
                const auto* tr = camEnt->GetComponent<
                    SceneEditor::TransformComponent>();
                const auto* cam = camEnt->GetComponent<
                    SceneEditor::CameraComponent>();
                if (tr && cam) {
                    m_viewCam.center =
                        ImVec2{tr->position.x, tr->position.y};
                    m_viewCam.zoom = cam->zoom;
                }
            }
            DrawViewportPanel();
            DrawSettingsPanel();
            DrawDialogPanel();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            return;
        }

        const ImGuiID dockId =
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        EnsureDefaultLayout(dockId);

        DrawMainMenuBar();
        HandleGlobalShortcuts();
        TickAssetFileWatcher(ImGui::GetIO().DeltaTime);
        TickScripts (ImGui::GetIO().DeltaTime);
        TickPhysics (ImGui::GetIO().DeltaTime);
        TickAnimators(ImGui::GetIO().DeltaTime);
        // M8c7: AudioSource auto-play. Fires on the rising
        // edge of Play (m_animPlaying false -> true) for any
        // enabled source flagged playOnStart.
        TickAudioSources(ImGui::GetIO().DeltaTime);
        ConsumePendingSceneLoad();
        // M9c2 particle systems. Same Play-mode gate as
        // animators -- emits + integrates while Play is on,
        // resets the pool back to empty when Play flips off
        // so the next play starts the burst cleanly.
        TickParticles(ImGui::GetIO().DeltaTime);
        TickSplash(ImGui::GetIO().DeltaTime);
        // M8c5: top up the music stream's buffer queue so a
        // streamed OGG / WAV keeps decoding past the
        // PlayMusic pre-fill (~280 ms of headroom at the
        // default 4096-frame chunk).
        SceneEditor::Audio::UpdateMusic();
        DrawHierarchyPanel();
        DrawInspectorPanel();
        DrawViewportPanel();
        DrawConsolePanel();
        DrawProjectPanel();
        DrawTilePalettePanel();
        DrawNewProjectModal();
        DrawSortingLayersModal();
        DrawCollisionMatrixModal();
        DrawAudioMixerModal();
        DrawEditorPreferencesModal();
        DrawSettingsPanel();
        DrawDialogPanel();
        DrawSpriteEditorWindow();
        DrawTilesetEditorWindow();
        DrawAutotileEditorWindow();
        DrawAnimationEditorWindow();
        DrawControllerEditorWindow();
        DrawScriptEditorWindow();
        if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Deferred native modal dialogs. Any Win32 modal that would
        // otherwise fire from inside a live BeginPopupModal / Begin
        // Popup runs here instead, after the ImGui DrawData is
        // committed to the backbuffer. This keeps the dialog on top
        // and interactive; see m_pendingBrowseForNewProjectPath.
        if (m_pendingBrowseForNewProjectPath) {
            m_pendingBrowseForNewProjectPath = false;
            const std::string folder =
                PickFolderDialog(m_hwnd, L"Pick parent folder");
            if (!folder.empty()) {
                std::snprintf(m_newProjectPath.data(),
                              m_newProjectPath.size(),
                              "%s", folder.c_str());
            }
        }
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

    // Build a "[k=v k=v ...]" payload string from whichever args of
    // an AnimationEvent differ from their defaults. Returned empty
    // when nothing's set.
    static std::string FormatAnimationEventArgs(
        const SceneEditor::AnimationEvent& ev) {
        std::string out;
        auto append = [&](const std::string& s) {
            if (!out.empty()) out += ' ';
            out += s;
        };
        char buf[128];
        if (!ev.stringArg.empty()) {
            std::snprintf(buf, sizeof(buf), "s=\"%s\"",
                          ev.stringArg.c_str());
            append(buf);
        }
        if (ev.floatArg != 0.0f) {
            std::snprintf(buf, sizeof(buf), "f=%.3f", double(ev.floatArg));
            append(buf);
        }
        if (ev.intArg != 0) {
            std::snprintf(buf, sizeof(buf), "i=%d", ev.intArg);
            append(buf);
        }
        if (ev.boolArg) append("b=true");
        if (ev.vec2Arg.x != 0.0f || ev.vec2Arg.y != 0.0f) {
            std::snprintf(buf, sizeof(buf), "v=(%.3f,%.3f)",
                          double(ev.vec2Arg.x), double(ev.vec2Arg.y));
            append(buf);
        }
        if (ev.colorArg.a != 0.0f) {
            std::snprintf(buf, sizeof(buf), "c=(%.2f,%.2f,%.2f,%.2f)",
                          double(ev.colorArg.r), double(ev.colorArg.g),
                          double(ev.colorArg.b), double(ev.colorArg.a));
            append(buf);
        }
        if (out.empty()) return out;
        return "[" + out + "]";
    }

    // Append one entry to the rolling event log. Trim from the front
    // so the tail (and Console rendering) stays bounded. Non-default
    // args get appended in a `[s="x" f=.. i=.. ...]` block so the
    // receiver can tell at a glance which payload reached them.
    // The same event also routes through m_scriptHost so the
    // scripting subsystem sees every dispatch -- M6c1 stubs the
    // host to a separate output log; M6c2+ replaces it with the
    // Lua bridge.
    void LogAnimationEvent(const SceneEditor::Entity& e,
                           const std::string& animName,
                           const SceneEditor::AnimationEvent& ev) {
        // Per-entity entry function takes priority; absent +
        // missing fields both fall back to the global
        // OnAnimationEvent inside the host.
        std::string entryFn;
        if (auto* sc =
                e.GetComponent<SceneEditor::ScriptComponent>()) {
            entryFn = sc->entryFunction;
        }
        const std::string& entityName = e.name;
        m_scriptHost.OnAnimationEvent(entityName, animName, ev,
                                      entryFn);
        const std::string args = FormatAnimationEventArgs(ev);
        char buf[512];
        if (!args.empty()) {
            std::snprintf(buf, sizeof(buf),
                          "%s | %s @ %.2fs : %s %s",
                          entityName.c_str(), animName.c_str(),
                          ev.time, ev.name.c_str(), args.c_str());
        } else {
            std::snprintf(buf, sizeof(buf),
                          "%s | %s @ %.2fs : %s",
                          entityName.c_str(), animName.c_str(),
                          ev.time, ev.name.c_str());
        }
        m_eventLog.emplace_back(buf);
        if (m_eventLog.size() > kEventLogMax) {
            m_eventLog.erase(m_eventLog.begin(),
                             m_eventLog.begin() +
                                 (m_eventLog.size() - kEventLogMax));
        }
    }

    // Fire every event in `anim` that sits inside the play interval
    // (prevT, curT]. Honours loop wrap when curT < prevT.
    void FireEventsBetween(const SceneEditor::Entity& e,
                           const SceneEditor::AnimationAsset& anim,
                           float prevT, float curT) {
        if (anim.events.empty()) return;
        const float total = anim.TotalDuration();
        // Pull the texture's display name out of the entity for the
        // log line. The animation asset doesn't carry its own name --
        // the file path on the AssetEntry would, but threading it
        // here would be noisy.
        const std::string& entName = e.name;
        const std::string  animName = "anim";
        auto fire = [&](float lo, float hi) {
            for (const auto& ev : anim.events) {
                if (ev.time >  lo && ev.time <= hi) {
                    LogAnimationEvent(e, animName, ev);
                }
            }
        };
        if (curT >= prevT) {
            fire(prevT, curT);
        } else {
            // Looped: fire the tail of the previous lap + the head of
            // the new one. Total > 0 because curT < prevT implies at
            // least one wrap.
            fire(prevT, total + 0.001f);
            fire(-1.0f, curT);
        }
    }

    // Advance every AnimatorComponent by `dt` seconds (when the global
    // Fires OnDestroy on a single entity (when it has a
    // ScriptComponent that already started) + recurses into its
    // children before they get destroyed. Mirrors the destroy
    // order Scene::DestroyEntity itself uses so scripts see
    // children torn down before their parents.
    void NotifyScriptDestroyForEntity(SceneEditor::EntityId id) {
        SceneEditor::Entity* e = m_scene.Get(id);
        if (!e) return;
        for (SceneEditor::EntityId childId : e->children) {
            NotifyScriptDestroyForEntity(childId);
        }
        if (auto* sc =
                e->GetComponent<SceneEditor::ScriptComponent>()) {
            // OnDisable runs first when the entity was alive +
            // enabled, mirroring Unity's
            // "OnDisable before OnDestroy" rule.
            if (sc->lastSeenEnabled) {
                m_scriptHost.CallOnDisable(e->name, sc->entryFunction);
                sc->lastSeenEnabled = false;
            }
            if (sc->started) {
                m_scriptHost.CallOnDestroy(e->name, sc->entryFunction);
                sc->started = false;
            }
        }
    }

    // Fires OnDestroy on every entity currently in the scene that
    // has a started ScriptComponent. Used right before any path
    // that wipes the scene wholesale (open, new, close project,
    // undo / redo's full-scene Deserialize).
    void NotifyAllScriptsForDestroy() {
        for (SceneEditor::EntityId rootId : m_scene.Roots()) {
            NotifyScriptDestroyForEntity(rootId);
        }
    }

    // Returns true when the watcher knows how to hot-reload the
    // given AssetEntry typeName. Filters the M6c23 sweep so the
    // mtime cache only tracks files we can actually do something
    // about on a change.
    static bool IsHotReloadableType(const std::string& typeName) {
        return typeName == "Script" ||
               typeName == "Animation" ||
               typeName == "AnimatorController" ||
               typeName == "Texture" ||
               typeName == "Tileset" ||
               typeName == "Autotile";
    }

    // Walks every entity that carries a BoxColliderComponent +
    // Transform, does an O(n^2) AABB overlap pass against every
    // other such entity, and diffs the resulting pair set
    // against m_collisionPairs to fire OnTriggerEnter on new
    // pairs and OnTriggerExit on ones that left contact. Both
    // sides of the pair receive the dispatch with the other
    // entity name as the second argument.
    // M7c14: physics runs at a fixed sub-step ratio so a fast
    // body (Rigidbody2D with a big velocity, or a script
    // calling transform.translate by a large delta) doesn't
    // tunnel through thin colliders inside a single render
    // frame. The integrator + collision detection both step
    // dt/kPhysicsSubSteps at a time; per-frame Lua callbacks
    // (Update, raycast, overlap) still see the full dt as
    // "this frame".
    static constexpr int kPhysicsSubSteps = 4;
    void TickPhysics(float dt) {
        const float subDt = dt / float(kPhysicsSubSteps);
        for (int i = 0; i < kPhysicsSubSteps; ++i) {
            TickRigidbodies(subDt);
            TickColliders  (subDt);
        }
    }

    // Semi-implicit Euler integrator for every entity that
    // carries a Rigidbody2D + Transform. Runs before
    // TickColliders so the M7c5 push-out path sees the
    // newly-stepped position before resolving overlaps.
    void TickRigidbodies(float dt) {
        if (!m_animPlaying) return;
        std::vector<SceneEditor::EntityId> ids;
        CollectAllEntities(m_scene.Roots(), ids);
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            auto* tr = e->GetComponent<SceneEditor::TransformComponent>();
            auto* rb = e->GetComponent<SceneEditor::Rigidbody2DComponent>();
            if (!tr || !rb) continue;
            rb->velocity.x += rb->gravity.x * dt;
            rb->velocity.y += rb->gravity.y * dt;
            const float damping = std::max(0.0f, 1.0f - rb->drag * dt);
            rb->velocity.x *= damping;
            rb->velocity.y *= damping;
            tr->position.x += rb->velocity.x * dt;
            tr->position.y += rb->velocity.y * dt;
        }
    }

    void TickColliders(float /*dt*/) {
        if (!m_animPlaying) return;
        // M7c27: Capsule joins Box + Circle. Stored as
        // (cx, cy, halfY, radius, cosR, sinR) where halfY is
        // the spine half-length along local Y; radius is the
        // hemispherical cap radius. Overlap + ComputeMTV
        // decompose it into a centre OBB + 2 endcap circles +
        // dispatch through the existing Box / Circle helpers.
        // M7c28: Polygon (convex) -- world-space vertex list
        // stored in `polygonVerts`. The M7c25 SatPolyBox + the
        // new SatPolyPoly fall through unchanged because both
        // already loop over an arbitrary vertex count.
        // M7c32: Edge (chain of segments) -- world-space verts
        // also stored in `polygonVerts` + `chainClosed` says
        // whether to wrap the last segment back to vertex 0.
        // Overlap + ComputeMTV decompose it into one thin OBB
        // per segment + reuse the existing Box paths.
        enum class ColliderKind { Box, Circle, Capsule, Polygon, Edge };
        struct ColliderInfo {
            SceneEditor::EntityId   id;
            std::string             name;
            std::string             entryFunction;
            bool                    hasScript;
            bool                    isTrigger;
            ColliderKind            kind;
            // Box: minX/maxX/minY/maxY are the world AABB.
            // Circle: cx/cy is the world centre, radius is in
            // world units; minX/maxX/minY/maxY hold the circle's
            // bounding AABB so the cheap AABB-vs-AABB cull at
            // the top of the pair loop still works for circles.
            float                   cx, cy;
            float                   minX, maxX, minY, maxY;
            float                   radius;
            // Cached velocity from the entity's Rigidbody2D
            // (zero for static colliders). ComputeMTV consults
            // it when the closest-point clamp degenerates --
            // the deep-tunnel case has to push back the way the
            // body came in, not toward the nearest face (which
            // for a fast falling ball would point through the
            // floor + out the underside).
            float                   vx, vy;
            // M7c11: collision layer for filtering.
            std::string             layer;
            // M7c13: oriented box. halfX / halfY live in the
            // collider's local frame; cosR / sinR rotate them
            // into world space. Circles set rotation = 0 and
            // halfX = halfY = radius so the bounding AABB
            // logic stays uniform.
            float                   halfX, halfY;
            float                   cosR, sinR;
            // M7c21: tile-driven OneWay flag. Only the per-tile
            // synthetic infos ever set this; the pair loop
            // suppresses contacts whose MTV would push the body
            // sideways or downward, so jump-through platforms
            // pass when the body's MTV doesn't point up.
            bool                    oneWay = false;
            // M7c22: tile-driven 45-degree slope shape. Box +
            // Circle bodies hitting a slope cell get pushed
            // perpendicular to the hypotenuse instead of along
            // the AABB faces.
            //   None       = not a slope (default)
            //   Right      = / hypotenuse, filled bottom-right
            //   Left       = \ hypotenuse, filled bottom-left
            //   RightHalf  = 22.5° / from bottom-left to mid-right
            //   LeftHalf   = 22.5° \ from bottom-right to mid-left
            //   RightHigh  = 22.5° / from mid-left to top-right
            //                (upper half of two-cell ramp)
            //   LeftHigh   = 22.5° \ from mid-right to top-left
            enum class SlopeShape {
                None,
                Right, Left,
                RightHalf, LeftHalf,
                RightHigh, LeftHigh
            };
            SlopeShape              slope = SlopeShape::None;
            // M7c28: convex polygon vertices in world space
            // (CCW). Empty for Box / Circle / Capsule / Slope.
            // The pair loop only walks this array when
            // kind == Polygon -- the field stays unset on the
            // hot path.
            // M7c32: Edge collider also stores its chain in
            // this field; chainClosed indicates whether a
            // final segment from vertex N-1 back to vertex 0
            // exists.
            std::vector<std::array<float, 2>> polygonVerts;
            bool chainClosed = false;
        };
        std::vector<ColliderInfo> infos;
        infos.reserve(32);
        std::vector<SceneEditor::EntityId> ids;
        CollectAllEntities(m_scene.Roots(), ids);
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            auto* tr = e->GetComponent<SceneEditor::TransformComponent>();
            if (!tr) continue;
            auto* bc = e->GetComponent<SceneEditor::BoxColliderComponent>();
            auto* cc = e->GetComponent<SceneEditor::CircleColliderComponent>();
            auto* capc = e->GetComponent<SceneEditor::CapsuleColliderComponent>();
            auto* pgc = e->GetComponent<SceneEditor::PolygonColliderComponent>();
            auto* ec = e->GetComponent<SceneEditor::EdgeColliderComponent>();
            if (!bc && !cc && !capc && !pgc && !ec) continue;
            auto* sc = e->GetComponent<SceneEditor::ScriptComponent>();
            auto* rbInfo = e->GetComponent<SceneEditor::Rigidbody2DComponent>();
            ColliderInfo ci;
            ci.id            = id;
            ci.name          = e->name;
            ci.hasScript     = (sc != nullptr);
            ci.entryFunction = sc ? sc->entryFunction : std::string{};
            ci.vx            = rbInfo ? rbInfo->velocity.x : 0.0f;
            ci.vy            = rbInfo ? rbInfo->velocity.y : 0.0f;
            if (bc) {
                const float halfX = bc->size.x * 0.5f * tr->scale.x;
                const float halfY = bc->size.y * 0.5f * tr->scale.y;
                ci.kind      = ColliderKind::Box;
                ci.isTrigger = bc->isTrigger;
                ci.layer     = bc->layer;
                ci.cx        = tr->position.x + bc->offset.x;
                ci.cy        = tr->position.y + bc->offset.y;
                ci.radius    = 0.0f;
                ci.halfX     = halfX;
                ci.halfY     = halfY;
                const float rotRad = tr->rotation
                    * 3.14159265358979323846f / 180.0f;
                ci.cosR      = std::cos(rotRad);
                ci.sinR      = std::sin(rotRad);
                // World-space bounding AABB of the rotated box
                // -- the four corners' min/max along world axes.
                // Used by the cheap top-level cull only; SAT
                // does the precise test.
                const float ex = std::fabs(ci.cosR) * halfX +
                                 std::fabs(ci.sinR) * halfY;
                const float ey = std::fabs(ci.sinR) * halfX +
                                 std::fabs(ci.cosR) * halfY;
                ci.minX      = ci.cx - ex;
                ci.maxX      = ci.cx + ex;
                ci.minY      = ci.cy - ey;
                ci.maxY      = ci.cy + ey;
            } else if (cc) {
                const float scaleAvg =
                    (tr->scale.x + tr->scale.y) * 0.5f;
                ci.kind      = ColliderKind::Circle;
                ci.isTrigger = cc->isTrigger;
                ci.layer     = cc->layer;
                ci.cx        = tr->position.x + cc->offset.x;
                ci.cy        = tr->position.y + cc->offset.y;
                ci.radius    = cc->radius * scaleAvg;
                ci.halfX     = ci.radius;
                ci.halfY     = ci.radius;
                ci.cosR      = 1.0f;
                ci.sinR      = 0.0f;
                ci.minX      = ci.cx - ci.radius;
                ci.maxX      = ci.cx + ci.radius;
                ci.minY      = ci.cy - ci.radius;
                ci.maxY      = ci.cy + ci.radius;
            } else if (capc) {
                // M7c27: capsule -- rotation flows in from the
                // Transform same as Box. halfY = spine
                // halfLength along local Y, radius = cap.
                const float scaleX = tr->scale.x;
                const float scaleY = tr->scale.y;
                const float scaleAvg = (scaleX + scaleY) * 0.5f;
                ci.kind      = ColliderKind::Capsule;
                ci.isTrigger = capc->isTrigger;
                ci.layer     = capc->layer;
                ci.cx        = tr->position.x + capc->offset.x;
                ci.cy        = tr->position.y + capc->offset.y;
                ci.radius    = capc->radius * scaleAvg;
                ci.halfX     = ci.radius;
                ci.halfY     = capc->halfLength * scaleY;
                const float rotRad = tr->rotation
                    * 3.14159265358979323846f / 180.0f;
                ci.cosR      = std::cos(rotRad);
                ci.sinR      = std::sin(rotRad);
                const float ex =
                    std::fabs(ci.cosR) * ci.radius +
                    std::fabs(ci.sinR) * (ci.halfY + ci.radius);
                const float ey =
                    std::fabs(ci.sinR) * ci.radius +
                    std::fabs(ci.cosR) * (ci.halfY + ci.radius);
                ci.minX      = ci.cx - ex;
                ci.maxX      = ci.cx + ex;
                ci.minY      = ci.cy - ey;
                ci.maxY      = ci.cy + ey;
            } else if (pgc) {
                // M7c28: convex polygon. Transform each local
                // vertex into world space + cache it on the
                // ColliderInfo so the n-gon SAT helpers don't
                // need to re-derive it per pair-loop iteration.
                if (pgc->vertices.size() < 3) continue;
                ci.kind      = ColliderKind::Polygon;
                ci.isTrigger = pgc->isTrigger;
                ci.layer     = pgc->layer;
                ci.cx        = tr->position.x + pgc->offset.x;
                ci.cy        = tr->position.y + pgc->offset.y;
                ci.radius    = 0.0f;
                const float rotRad = tr->rotation
                    * 3.14159265358979323846f / 180.0f;
                ci.cosR      = std::cos(rotRad);
                ci.sinR      = std::sin(rotRad);
                const float sx = tr->scale.x;
                const float sy = tr->scale.y;
                ci.polygonVerts.reserve(pgc->vertices.size());
                float minX = std::numeric_limits<float>::infinity();
                float maxX = -minX;
                float minY = minX;
                float maxY = -minY;
                for (const auto& lv : pgc->vertices) {
                    const float lxs = lv.x * sx;
                    const float lys = lv.y * sy;
                    const float wx = ci.cx +
                        ci.cosR * lxs - ci.sinR * lys;
                    const float wy = ci.cy +
                        ci.sinR * lxs + ci.cosR * lys;
                    ci.polygonVerts.push_back({ wx, wy });
                    minX = std::min(minX, wx);
                    maxX = std::max(maxX, wx);
                    minY = std::min(minY, wy);
                    maxY = std::max(maxY, wy);
                }
                ci.halfX = (maxX - minX) * 0.5f;
                ci.halfY = (maxY - minY) * 0.5f;
                ci.minX  = minX;
                ci.maxX  = maxX;
                ci.minY  = minY;
                ci.maxY  = maxY;
            } else {
                // M7c32: edge / chain collider. Same world-
                // space vertex caching the Polygon path uses,
                // plus a chainClosed flag the decomposer reads
                // to decide whether to wrap the final segment.
                if (ec->vertices.size() < 2) continue;
                ci.kind         = ColliderKind::Edge;
                ci.isTrigger    = ec->isTrigger;
                ci.layer        = ec->layer;
                ci.cx           = tr->position.x + ec->offset.x;
                ci.cy           = tr->position.y + ec->offset.y;
                ci.radius       = 0.0f;
                ci.chainClosed  = ec->closed;
                const float rotRad = tr->rotation
                    * 3.14159265358979323846f / 180.0f;
                ci.cosR      = std::cos(rotRad);
                ci.sinR      = std::sin(rotRad);
                const float sx = tr->scale.x;
                const float sy = tr->scale.y;
                ci.polygonVerts.reserve(ec->vertices.size());
                float minX = std::numeric_limits<float>::infinity();
                float maxX = -minX;
                float minY = minX;
                float maxY = -minY;
                for (const auto& lv : ec->vertices) {
                    const float lxs = lv.x * sx;
                    const float lys = lv.y * sy;
                    const float wx = ci.cx +
                        ci.cosR * lxs - ci.sinR * lys;
                    const float wy = ci.cy +
                        ci.sinR * lxs + ci.cosR * lys;
                    ci.polygonVerts.push_back({ wx, wy });
                    minX = std::min(minX, wx);
                    maxX = std::max(maxX, wx);
                    minY = std::min(minY, wy);
                    maxY = std::max(maxY, wy);
                }
                // Pad the bounding AABB by the segment
                // thickness so a body brushing the edge
                // still triggers the cheap top-level cull.
                const float pad = 0.05f;
                ci.halfX = (maxX - minX) * 0.5f;
                ci.halfY = (maxY - minY) * 0.5f;
                ci.minX  = minX - pad;
                ci.maxX  = maxX + pad;
                ci.minY  = minY - pad;
                ci.maxY  = maxY + pad;
            }
            infos.push_back(std::move(ci));
        }
        // M7c18: synthetic per-tile colliders. Each tilemap cell
        // whose tile carries `collides = true` in the Tileset
        // asset becomes a static Box collider in the pair loop.
        // Synthetic ids encode (tilemap, gx, gy) so the same
        // cell keeps the same id across frames + Enter/Stay/Exit
        // diff against m_collisionPairs works naturally. The
        // synthetic side is marked hasScript=false so only the
        // body that collides with the wall fires events.
        m_syntheticColliderNames.clear();
        auto SyntheticCellId = [](SceneEditor::EntityId tilemap,
                                  int gx, int gy) -> SceneEditor::EntityId {
            const uint64_t hi  = 1ULL << 63;
            const uint64_t tm  = (uint64_t(tilemap) & 0x7FFFFFFFu) << 32;
            const uint64_t ex  = uint64_t(uint16_t(gx)) << 16;
            const uint64_t ey  = uint64_t(uint16_t(gy));
            return SceneEditor::EntityId(hi | tm | ex | ey);
        };
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            auto* tr = e->GetComponent<SceneEditor::TransformComponent>();
            auto* tm = e->GetComponent<SceneEditor::TilemapComponent>();
            if (!tr || !tm) continue;
            // Tileset must resolve to a TilesetAsset (not a raw
            // texture) for per-tile metadata to exist.
            const auto* tsEntry = m_assetDb.FindByGuid(tm->tileset);
            if (!tsEntry || tsEntry->typeName != "Tileset") continue;
            SceneEditor::TilesetAsset* ts =
                m_assetDb.GetTileset(tm->tileset);
            if (!ts) continue;
            // World units per cell.
            SceneEditor::TextureImportSettings imp;
            if (const auto* texEntry =
                    m_assetDb.FindByGuid(ts->texture)) {
                imp.FromJson(texEntry->importSettings);
            }
            const float cellWorldW =
                float(ts->cellPixelW) / imp.pixelsPerUnit;
            const float cellWorldH =
                float(ts->cellPixelH) / imp.pixelsPerUnit;
            const float rotRad = tr->rotation
                * 3.14159265358979323846f / 180.0f;
            const float cosR = std::cos(rotRad);
            const float sinR = std::sin(rotRad);
            const float halfX = cellWorldW * 0.5f * tr->scale.x;
            const float halfY = cellWorldH * 0.5f * tr->scale.y;
            for (const auto& kv : tm->cells) {
                const int gx = kv.first.first;
                const int gy = kv.first.second;
                const int idx = kv.second;
                auto tIt = ts->tiles.find(idx);
                if (tIt == ts->tiles.end()) continue;
                // M7c19: a tile contributes a collider when
                // either flag is set. isTrigger drives the
                // synthetic collider's Trigger / Solid mode.
                if (!tIt->second.collides && !tIt->second.isTrigger)
                    continue;
                const bool tileTrigger = tIt->second.isTrigger;
                const bool tileOneWay =
                    !tileTrigger &&
                    tIt->second.shape ==
                        SceneEditor::TileColliderShape::OneWay;
                ColliderInfo::SlopeShape tileSlope =
                    ColliderInfo::SlopeShape::None;
                if (!tileTrigger) {
                    using TileShape =
                        SceneEditor::TileColliderShape;
                    switch (tIt->second.shape) {
                    case TileShape::SlopeRight:
                        tileSlope = ColliderInfo::SlopeShape::Right;
                        break;
                    case TileShape::SlopeLeft:
                        tileSlope = ColliderInfo::SlopeShape::Left;
                        break;
                    case TileShape::SlopeRightHalf:
                        tileSlope = ColliderInfo::SlopeShape::RightHalf;
                        break;
                    case TileShape::SlopeLeftHalf:
                        tileSlope = ColliderInfo::SlopeShape::LeftHalf;
                        break;
                    case TileShape::SlopeRightHigh:
                        tileSlope = ColliderInfo::SlopeShape::RightHigh;
                        break;
                    case TileShape::SlopeLeftHigh:
                        tileSlope = ColliderInfo::SlopeShape::LeftHigh;
                        break;
                    default:
                        break;
                    }
                }
                // Cell centre in entity-local coordinates, then
                // rotate into world.
                const float lx = (float(gx) + 0.5f) * cellWorldW;
                const float ly = (float(gy) + 0.5f) * cellWorldH;
                const float wx = tr->position.x +
                    cosR * lx - sinR * ly;
                const float wy = tr->position.y +
                    sinR * lx + cosR * ly;
                ColliderInfo ci;
                ci.id            = SyntheticCellId(id, gx, gy);
                ci.name          = e->name;
                ci.hasScript     = false;
                ci.entryFunction.clear();
                ci.isTrigger     = tileTrigger;
                ci.oneWay        = tileOneWay;
                ci.slope         = tileSlope;
                ci.kind          = ColliderKind::Box;
                ci.layer         = tm->collisionLayer;
                ci.cx            = wx;
                ci.cy            = wy;
                ci.radius        = 0.0f;
                ci.vx            = 0.0f;
                ci.vy            = 0.0f;
                ci.halfX         = halfX;
                ci.halfY         = halfY;
                ci.cosR          = cosR;
                ci.sinR          = sinR;
                const float ex = std::fabs(cosR) * halfX +
                                 std::fabs(sinR) * halfY;
                const float ey = std::fabs(sinR) * halfX +
                                 std::fabs(cosR) * halfY;
                ci.minX = wx - ex;
                ci.maxX = wx + ex;
                ci.minY = wy - ey;
                ci.maxY = wy + ey;
                m_syntheticColliderNames[ci.id] = e->name;
                infos.push_back(std::move(ci));
            }
        }
        // Box-vs-axis projection. Returns (min, max) of the
        // box's footprint along the unit axis (ax, ay). The
        // half-extent along the axis is the sum of each local
        // axis's |dot| with the test axis times its half-size,
        // so a box rotated 45 deg projects sqrt(2)*halfX onto
        // the world X axis -- the SAT invariant.
        auto ProjectBox = [](const ColliderInfo& box,
                             float ax, float ay,
                             float& outMin, float& outMax) {
            const float centerProj = box.cx * ax + box.cy * ay;
            const float xExt = box.halfX *
                std::fabs(box.cosR * ax + box.sinR * ay);
            const float yExt = box.halfY *
                std::fabs(-box.sinR * ax + box.cosR * ay);
            const float ext = xExt + yExt;
            outMin = centerProj - ext;
            outMax = centerProj + ext;
        };

        // M7c13: Separating Axis Theorem for OBB-vs-OBB.
        // Returns true + the MTV that pushes `a` out of `b`
        // when the two boxes overlap. Falls through to false
        // the moment any of the four candidate axes finds a
        // separating gap, so the early-out is fast.
        auto SatBoxBox = [&ProjectBox](const ColliderInfo& a,
                                       const ColliderInfo& b,
                                       float& mtvX, float& mtvY) {
            const float axes[4][2] = {
                {  a.cosR,  a.sinR },
                { -a.sinR,  a.cosR },
                {  b.cosR,  b.sinR },
                { -b.sinR,  b.cosR },
            };
            float minOverlap = std::numeric_limits<float>::infinity();
            float bestAx = 0.0f, bestAy = 0.0f;
            for (int i = 0; i < 4; ++i) {
                float aMin, aMax, bMin, bMax;
                ProjectBox(a, axes[i][0], axes[i][1], aMin, aMax);
                ProjectBox(b, axes[i][0], axes[i][1], bMin, bMax);
                const float ov = std::min(aMax, bMax) - std::max(aMin, bMin);
                if (ov < 0.0f) return false;
                if (ov < minOverlap) {
                    minOverlap = ov;
                    bestAx = axes[i][0];
                    bestAy = axes[i][1];
                }
            }
            // Orient the MTV so it pushes `a` *away* from `b`
            // (flip the axis when `b` sits on the +axis side
            // of `a`).
            const float d = (b.cx - a.cx) * bestAx +
                            (b.cy - a.cy) * bestAy;
            if (d > 0.0f) { bestAx = -bestAx; bestAy = -bestAy; }
            mtvX = bestAx * minOverlap;
            mtvY = bestAy * minOverlap;
            return true;
        };

        // Minimum translation vector returning (dx, dy) such that
        // moving entity A by (dx, dy) separates it from B.
        // Caller pushes B by the opposite vector. Used by the
        // solid (non-trigger) push-out path.
        // M7c23: slope shape parameterised by per-shape
        // (leftH, rightH) pair. Defined before the MTV helper
        // so the lambda capture works without forward refs.
        auto SlopeHeights = [](ColliderInfo::SlopeShape s,
                               float& leftH, float& rightH) {
            switch (s) {
            case ColliderInfo::SlopeShape::Right:
                leftH = 0.0f; rightH = 1.0f; break;
            case ColliderInfo::SlopeShape::Left:
                leftH = 1.0f; rightH = 0.0f; break;
            case ColliderInfo::SlopeShape::RightHalf:
                leftH = 0.0f; rightH = 0.5f; break;
            case ColliderInfo::SlopeShape::LeftHalf:
                leftH = 0.5f; rightH = 0.0f; break;
            case ColliderInfo::SlopeShape::RightHigh:
                leftH = 0.5f; rightH = 1.0f; break;
            case ColliderInfo::SlopeShape::LeftHigh:
                leftH = 1.0f; rightH = 0.5f; break;
            default:
                leftH = 0.0f; rightH = 0.0f; break;
            }
        };
        auto SlopeHypotenuse = [&SlopeHeights](
                                const ColliderInfo& slope,
                                float& P0x, float& P0y,
                                float& P1x, float& P1y,
                                float& outNx, float& outNy) {
            float leftH, rightH;
            SlopeHeights(slope.slope, leftH, rightH);
            const float fullH = 2.0f * slope.halfY;
            const float bl_x = slope.cx - slope.halfX;
            const float bl_y = slope.cy - slope.halfY;
            const float br_x = slope.cx + slope.halfX;
            const float br_y = slope.cy - slope.halfY;
            P0x = bl_x;
            P0y = bl_y + leftH * fullH;
            P1x = br_x;
            P1y = br_y + rightH * fullH;
            const float dx = P1x - P0x;
            const float dy = P1y - P0y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-6f) {
                outNx = 0.0f; outNy = 1.0f;
                return;
            }
            // Rotate hypotenuse direction 90-deg CCW. Filled
            // triangle sits below the hypotenuse, so the
            // resulting normal points up-and-outward for both
            // / and \ orientations + half variants.
            outNx = -dy / len;
            outNy =  dx / len;
        };

        // M7c24: triangle-vs-OBB SAT for the slope-vs-box
        // case. Five candidate axes -- the box's two local
        // axes + the triangle's three edge normals. For each
        // edge we compute a perpendicular + flip toward the
        // outward side (away from the opposite vertex). The
        // axis with the smallest projection overlap drives the
        // MTV; the sign is oriented so the result pushes the
        // box (the second argument) away from the triangle
        // (the first argument). Caller flips the sign as needed
        // to match ComputeMTV's "move `a` away from `b`"
        // convention.
        // M7c25: PolyVertices replaces TriangleVertices. For
        // half / 45-deg variants (leftH=0 or rightH=0) the
        // filled region is a triangle (3 vertices); for High
        // variants (leftH>0 and rightH>0) it's a trapezoid
        // (4 vertices) -- the cell's bottom edge sits flush
        // with the cell floor + the top edge follows the
        // slope hypotenuse. CCW vertex order so the cross-
        // product centroid math stays consistent across both.
        // M7c28: generalised to handle Polygon collider too.
        // Buffer enlarged from [4][2] to [16][2] so any convex
        // shape with up to 16 vertices flows through the same
        // helpers (Polygon, slope triangle, slope trapezoid).
        auto PolyVertices = [&SlopeHeights](
                                const ColliderInfo& info,
                                float v[16][2],
                                int& count) {
            if (info.kind == ColliderKind::Polygon) {
                const int n = std::min(
                    int(info.polygonVerts.size()), 16);
                for (int i = 0; i < n; ++i) {
                    v[i][0] = info.polygonVerts[i][0];
                    v[i][1] = info.polygonVerts[i][1];
                }
                count = n;
                return;
            }
            float leftH, rightH;
            SlopeHeights(info.slope, leftH, rightH);
            const float fullH = 2.0f * info.halfY;
            const float bl_x = info.cx - info.halfX;
            const float bl_y = info.cy - info.halfY;
            const float br_x = info.cx + info.halfX;
            const float br_y = info.cy - info.halfY;
            // Common bottom corners (CCW order).
            v[0][0] = bl_x; v[0][1] = bl_y;
            v[1][0] = br_x; v[1][1] = br_y;
            if (leftH <= 0.0f) {
                // Triangle: BL, BR, top-right.
                v[2][0] = br_x;
                v[2][1] = br_y + rightH * fullH;
                count = 3;
            } else if (rightH <= 0.0f) {
                // Triangle: BL, BR, top-left.
                v[2][0] = bl_x;
                v[2][1] = bl_y + leftH * fullH;
                count = 3;
            } else {
                // Trapezoid: BL, BR, TR_at_rightH, TL_at_leftH.
                v[2][0] = br_x;
                v[2][1] = br_y + rightH * fullH;
                v[3][0] = bl_x;
                v[3][1] = bl_y + leftH * fullH;
                count = 4;
            }
        };
        // SAT for any convex polygon (3 or 4 vertices) vs OBB.
        // Generalises the M7c24 SatTriBox to handle both
        // triangles (Half + 45-deg slopes) + trapezoids (High
        // slopes) without per-shape branching elsewhere.
        auto SatPolyBox = [&ProjectBox, &PolyVertices](
                            const ColliderInfo& poly,
                            const ColliderInfo& box,
                            float& mtvX, float& mtvY) {
            float v[16][2];
            int n;
            PolyVertices(poly, v, n);
            // 2 box axes + n edge normals.
            float axes[18][2];
            axes[0][0] =  box.cosR; axes[0][1] =  box.sinR;
            axes[1][0] = -box.sinR; axes[1][1] =  box.cosR;
            // Centroid of the polygon (average of vertices).
            float cx = 0.0f, cy = 0.0f;
            for (int i = 0; i < n; ++i) {
                cx += v[i][0]; cy += v[i][1];
            }
            cx /= float(n); cy /= float(n);
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                const float ex = v[j][0] - v[i][0];
                const float ey = v[j][1] - v[i][1];
                // Perpendicular pointing away from the centroid
                // (= outward normal of the convex polygon).
                float nx = -ey, ny = ex;
                const float mx = (v[i][0] + v[j][0]) * 0.5f - cx;
                const float my = (v[i][1] + v[j][1]) * 0.5f - cy;
                if (nx * mx + ny * my < 0.0f) {
                    nx = -nx; ny = -ny;
                }
                const float len = std::sqrt(nx * nx + ny * ny);
                if (len < 1e-6f) { nx = 1.0f; ny = 0.0f; }
                else             { nx /= len; ny /= len; }
                axes[2 + i][0] = nx;
                axes[2 + i][1] = ny;
            }
            const int axisCount = 2 + n;
            float minOverlap = std::numeric_limits<float>::infinity();
            float bestNx = 0.0f, bestNy = 0.0f;
            for (int i = 0; i < axisCount; ++i) {
                const float ax = axes[i][0];
                const float ay = axes[i][1];
                float polyMin = v[0][0] * ax + v[0][1] * ay;
                float polyMax = polyMin;
                for (int k = 1; k < n; ++k) {
                    const float p = v[k][0] * ax + v[k][1] * ay;
                    polyMin = std::min(polyMin, p);
                    polyMax = std::max(polyMax, p);
                }
                float boxMin, boxMax;
                ProjectBox(box, ax, ay, boxMin, boxMax);
                const float overlap = std::min(polyMax, boxMax) -
                                      std::max(polyMin, boxMin);
                if (overlap < 0.0f) return false;
                if (overlap < minOverlap) {
                    minOverlap = overlap;
                    bestNx = ax; bestNy = ay;
                }
            }
            // Orient MTV pushing the BOX away from polygon's
            // centroid (already computed above).
            const float d = (box.cx - cx) * bestNx +
                            (box.cy - cy) * bestNy;
            if (d < 0.0f) { bestNx = -bestNx; bestNy = -bestNy; }
            mtvX = bestNx * minOverlap;
            mtvY = bestNy * minOverlap;
            return true;
        };
        // Back-compat alias so the existing wiring keeps
        // building until I update the call sites below.
        auto SatTriBox = SatPolyBox;

        // M7c28: convex polygon vs convex polygon SAT. Uses
        // both polygons' edge normals as candidate axes; the
        // result MTV pushes A away from B. Falls through for
        // Polygon-vs-Polygon, Polygon-vs-Slope (slopes also
        // route through PolyVertices), and Slope-vs-Polygon.
        auto SatPolyPoly = [&PolyVertices](
                            const ColliderInfo& a,
                            const ColliderInfo& b,
                            float& mtvX, float& mtvY) {
            float va[16][2], vb[16][2];
            int na, nb;
            PolyVertices(a, va, na);
            PolyVertices(b, vb, nb);
            if (na < 3 || nb < 3) return false;
            // Centroids for outward normal disambiguation +
            // final MTV sign.
            float caX = 0.0f, caY = 0.0f;
            for (int i = 0; i < na; ++i) {
                caX += va[i][0]; caY += va[i][1];
            }
            caX /= float(na); caY /= float(na);
            float cbX = 0.0f, cbY = 0.0f;
            for (int i = 0; i < nb; ++i) {
                cbX += vb[i][0]; cbY += vb[i][1];
            }
            cbX /= float(nb); cbY /= float(nb);
            // Build axes from A's edge normals + B's edge
            // normals. CCW polygons => outward normal is the
            // 90-deg CW rotation of (v_next - v_curr); we
            // disambiguate via the centroid to keep the math
            // robust against accidental CW input.
            auto buildAxes = [&](const float v[16][2], int n,
                                 float cx, float cy,
                                 float axes[16][2]) {
                for (int i = 0; i < n; ++i) {
                    const int j = (i + 1) % n;
                    const float ex = v[j][0] - v[i][0];
                    const float ey = v[j][1] - v[i][1];
                    float nx = -ey, ny = ex;
                    const float mx = (v[i][0] + v[j][0]) * 0.5f - cx;
                    const float my = (v[i][1] + v[j][1]) * 0.5f - cy;
                    if (nx * mx + ny * my < 0.0f) {
                        nx = -nx; ny = -ny;
                    }
                    const float len = std::sqrt(nx * nx + ny * ny);
                    if (len < 1e-6f) { nx = 1.0f; ny = 0.0f; }
                    else             { nx /= len; ny /= len; }
                    axes[i][0] = nx;
                    axes[i][1] = ny;
                }
            };
            float axesA[16][2], axesB[16][2];
            buildAxes(va, na, caX, caY, axesA);
            buildAxes(vb, nb, cbX, cbY, axesB);
            float minOverlap = std::numeric_limits<float>::infinity();
            float bestNx = 0.0f, bestNy = 0.0f;
            auto testAxis = [&](float ax, float ay) -> bool {
                float aMin = va[0][0] * ax + va[0][1] * ay;
                float aMax = aMin;
                for (int k = 1; k < na; ++k) {
                    const float p = va[k][0] * ax + va[k][1] * ay;
                    aMin = std::min(aMin, p);
                    aMax = std::max(aMax, p);
                }
                float bMin = vb[0][0] * ax + vb[0][1] * ay;
                float bMax = bMin;
                for (int k = 1; k < nb; ++k) {
                    const float p = vb[k][0] * ax + vb[k][1] * ay;
                    bMin = std::min(bMin, p);
                    bMax = std::max(bMax, p);
                }
                const float overlap = std::min(aMax, bMax) -
                                      std::max(aMin, bMin);
                if (overlap < 0.0f) return false;
                if (overlap < minOverlap) {
                    minOverlap = overlap;
                    bestNx = ax; bestNy = ay;
                }
                return true;
            };
            for (int i = 0; i < na; ++i) {
                if (!testAxis(axesA[i][0], axesA[i][1])) return false;
            }
            for (int i = 0; i < nb; ++i) {
                if (!testAxis(axesB[i][0], axesB[i][1])) return false;
            }
            // MTV points from B's centroid -> A's centroid.
            const float d = (caX - cbX) * bestNx +
                            (caY - cbY) * bestNy;
            if (d < 0.0f) { bestNx = -bestNx; bestNy = -bestNy; }
            mtvX = bestNx * minOverlap;
            mtvY = bestNy * minOverlap;
            return true;
        };

        // M7c22: slope-vs-circle MTV. Pushes the circle out
        // perpendicular to the slope hypotenuse + radial at
        // segment endpoints. Returned vector moves `a` away
        // from `b`; when `a` is the slope (static), Resolve's
        // share=0 logic drops the slope-side push + the circle
        // gets pushed by the opposite vector.
        // M7c25: closest-point-on-polygon helper, defined
        // before SlopeCircleMtv so the lambda capture works
        // without a forward declaration.
        auto PolyCircleClosest = [&PolyVertices](
                const ColliderInfo& slope,
                const ColliderInfo& circ,
                float& outCx, float& outCy,
                bool& inside) {
            float v[16][2];
            int n;
            PolyVertices(slope, v, n);
            float ctrX = 0.0f, ctrY = 0.0f;
            for (int i = 0; i < n; ++i) {
                ctrX += v[i][0]; ctrY += v[i][1];
            }
            ctrX /= float(n); ctrY /= float(n);
            inside = true;
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                const float ex = v[j][0] - v[i][0];
                const float ey = v[j][1] - v[i][1];
                const float c1 = ex * (circ.cy - v[i][1]) -
                                 ey * (circ.cx - v[i][0]);
                const float c2 = ex * (ctrY - v[i][1]) -
                                 ey * (ctrX - v[i][0]);
                if ((c1 < 0.0f) != (c2 < 0.0f)) {
                    inside = false; break;
                }
            }
            float bestDistSq = std::numeric_limits<float>::infinity();
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                const float ex = v[j][0] - v[i][0];
                const float ey = v[j][1] - v[i][1];
                const float lenSq = ex * ex + ey * ey;
                if (lenSq < 1e-6f) continue;
                float t = ((circ.cx - v[i][0]) * ex +
                           (circ.cy - v[i][1]) * ey) / lenSq;
                t = std::clamp(t, 0.0f, 1.0f);
                const float pcx = v[i][0] + t * ex;
                const float pcy = v[i][1] + t * ey;
                const float ddx = circ.cx - pcx;
                const float ddy = circ.cy - pcy;
                const float dSq = ddx * ddx + ddy * ddy;
                if (dSq < bestDistSq) {
                    bestDistSq = dSq;
                    outCx = pcx; outCy = pcy;
                }
            }
        };
        auto SlopeCircleMtv = [&SlopeHypotenuse, &PolyCircleClosest](
                                const ColliderInfo& slope,
                                const ColliderInfo& circ,
                                float& outX, float& outY) {
            // M7c25: for trapezoid (High) slopes use the
            // closest-point-on-polygon approach -- the circle
            // could touch any of the 4 edges. For triangle
            // slopes the legacy segment-clamp path still
            // applies + the math collapses to the same result.
            // M7c28: convex polygons take the same closest-
            // point branch since their boundary has many
            // edges + the closest one drives the push.
            if (slope.kind == ColliderKind::Polygon ||
                slope.slope == ColliderInfo::SlopeShape::RightHigh ||
                slope.slope == ColliderInfo::SlopeShape::LeftHigh) {
                float pcx, pcy;
                bool inside;
                PolyCircleClosest(slope, circ, pcx, pcy, inside);
                const float ddx = circ.cx - pcx;
                const float ddy = circ.cy - pcy;
                const float distSq = ddx * ddx + ddy * ddy;
                const float dist = std::sqrt(distSq);
                float pushX = 0.0f, pushY = 0.0f;
                if (inside) {
                    // Push outward along (pc -> centre) -- the
                    // closest edge surface direction.
                    if (dist > 1e-6f) {
                        pushX = ddx / dist;
                        pushY = ddy / dist;
                    } else {
                        pushX = 0.0f; pushY = 1.0f;
                    }
                    const float pen = circ.radius + dist;
                    outX = pushX * pen;
                    outY = pushY * pen;
                } else {
                    if (dist >= circ.radius) {
                        outX = 0.0f; outY = 0.0f;
                        return;
                    }
                    pushX = ddx / dist;
                    pushY = ddy / dist;
                    const float pen = circ.radius - dist;
                    outX = pushX * pen;
                    outY = pushY * pen;
                }
                return;
            }
            float P0x, P0y, P1x, P1y, nx, ny;
            SlopeHypotenuse(slope, P0x, P0y, P1x, P1y, nx, ny);
            const float sx = P1x - P0x;
            const float sy = P1y - P0y;
            const float segLenSq = sx * sx + sy * sy;
            float t = ((circ.cx - P0x) * sx + (circ.cy - P0y) * sy)
                       / segLenSq;
            t = std::clamp(t, 0.0f, 1.0f);
            const float pcx = P0x + t * sx;
            const float pcy = P0y + t * sy;
            const float ddx = circ.cx - pcx;
            const float ddy = circ.cy - pcy;
            const float dist = std::sqrt(ddx * ddx + ddy * ddy);
            const float pen  = circ.radius - dist;
            // pen <= 0 means no real penetration but the
            // contact pair predicate still tripped due to
            // numerical edges; emit a tiny zero MTV.
            if (pen <= 0.0f) { outX = 0.0f; outY = 0.0f; return; }
            // The push direction for the circle is the outward
            // normal when t is in the segment interior + the
            // radial direction (away from the endpoint) when
            // clamped to an endpoint.
            float pushX, pushY;
            if (t > 0.0f && t < 1.0f) {
                pushX = nx; pushY = ny;
            } else if (dist > 1e-6f) {
                pushX = ddx / dist;
                pushY = ddy / dist;
            } else {
                pushX = nx; pushY = ny;
            }
            outX = pushX * pen;
            outY = pushY * pen;
        };

        // M7c27: build three sub-ColliderInfos for a capsule --
        // a centre OBB (size 2r x 2halfLen) + two endcap
        // circles. Endcaps live at the capsule centre +/- the
        // local-Y direction (-sinR, cosR) scaled by halfLen.
        // Defined before ComputeMTV so the lambda capture works
        // without a forward declaration.
        auto CapsuleDecompose = [](const ColliderInfo& cap,
                                   ColliderInfo* out) {
            const float dirX = -cap.sinR;
            const float dirY =  cap.cosR;
            ColliderInfo core = cap;
            core.kind   = ColliderKind::Box;
            core.halfX  = cap.radius;
            core.halfY  = cap.halfY;
            const float exA =
                std::fabs(core.cosR) * core.halfX +
                std::fabs(core.sinR) * core.halfY;
            const float eyA =
                std::fabs(core.sinR) * core.halfX +
                std::fabs(core.cosR) * core.halfY;
            core.minX = core.cx - exA;
            core.maxX = core.cx + exA;
            core.minY = core.cy - eyA;
            core.maxY = core.cy + eyA;
            out[0] = core;
            for (int i = 0; i < 2; ++i) {
                const float sign = (i == 0) ? -1.0f : 1.0f;
                ColliderInfo cap_i = cap;
                cap_i.kind   = ColliderKind::Circle;
                cap_i.cx     = cap.cx + sign * dirX * cap.halfY;
                cap_i.cy     = cap.cy + sign * dirY * cap.halfY;
                cap_i.halfX  = cap.radius;
                cap_i.halfY  = cap.radius;
                cap_i.cosR   = 1.0f;
                cap_i.sinR   = 0.0f;
                cap_i.minX   = cap_i.cx - cap.radius;
                cap_i.maxX   = cap_i.cx + cap.radius;
                cap_i.minY   = cap_i.cy - cap.radius;
                cap_i.maxY   = cap_i.cy + cap.radius;
                out[1 + i] = cap_i;
            }
        };

        // M7c32: decompose an Edge into one thin OBB per
        // segment so the existing SatBoxBox + box-vs-circle
        // paths can serve edge collision unchanged. Thickness
        // = 0.02 world units; small enough that a Circle
        // rolling on the edge sits within 0.01 of where the
        // analytical segment would put it, but non-zero so
        // SAT doesn't degenerate. Closed chains wrap the last
        // segment back to vertex 0.
        auto EdgeDecompose = [](const ColliderInfo& edge,
                                std::vector<ColliderInfo>& out) {
            out.clear();
            const int n = int(edge.polygonVerts.size());
            if (n < 2) return;
            const int segCount = edge.chainClosed ? n : n - 1;
            out.reserve(segCount);
            const float thickness = 0.02f;
            for (int i = 0; i < segCount; ++i) {
                const int j = (i + 1) % n;
                const float x0 = edge.polygonVerts[i][0];
                const float y0 = edge.polygonVerts[i][1];
                const float x1 = edge.polygonVerts[j][0];
                const float y1 = edge.polygonVerts[j][1];
                const float dx = x1 - x0;
                const float dy = y1 - y0;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len < 1e-6f) continue;
                ColliderInfo seg = edge;
                seg.kind   = ColliderKind::Box;
                seg.cx     = (x0 + x1) * 0.5f;
                seg.cy     = (y0 + y1) * 0.5f;
                seg.halfX  = len * 0.5f;
                seg.halfY  = thickness;
                // Local X axis aligned with the segment so the
                // OBB's long side hugs the polyline.
                seg.cosR   = dx / len;
                seg.sinR   = dy / len;
                const float ex =
                    std::fabs(seg.cosR) * seg.halfX +
                    std::fabs(seg.sinR) * seg.halfY;
                const float ey =
                    std::fabs(seg.sinR) * seg.halfX +
                    std::fabs(seg.cosR) * seg.halfY;
                seg.minX = seg.cx - ex;
                seg.maxX = seg.cx + ex;
                seg.minY = seg.cy - ey;
                seg.maxY = seg.cy + ey;
                seg.slope = ColliderInfo::SlopeShape::None;
                seg.polygonVerts.clear();
                seg.chainClosed = false;
                out.push_back(std::move(seg));
            }
        };

        auto ComputeMTV = [&SatBoxBox, &SlopeCircleMtv, &SatTriBox,
                           &CapsuleDecompose, &EdgeDecompose, &SatPolyPoly](
                            const ColliderInfo& a,
                            const ColliderInfo& b,
                            float& dx, float& dy) {
            dx = 0.0f; dy = 0.0f;
            // M7c27 capsule decomp: when either side is a
            // capsule, expand it into core OBB + 2 endcap
            // circles, compute MTV for every sub-pair, and
            // return the sub-pair whose push depth is largest.
            // The non-capsule path lives below in `testPair` so
            // the dispatch stays a single in-place expansion.
            auto testPair = [&](const ColliderInfo& a,
                                const ColliderInfo& b,
                                float& dx, float& dy) {
            dx = 0.0f; dy = 0.0f;
            const bool aSlope =
                a.slope != ColliderInfo::SlopeShape::None;
            const bool bSlope =
                b.slope != ColliderInfo::SlopeShape::None;
            // M7c28: polygon side uses the n-gon helpers the
            // same way slopes do; SatPolyPoly handles both
            // poly-vs-poly + poly-vs-slope, SatTriBox handles
            // poly-vs-box, SlopeCircleMtv handles poly-vs-
            // circle via PolyCircleClosest.
            const bool aPoly = (a.kind == ColliderKind::Polygon);
            const bool bPoly = (b.kind == ColliderKind::Polygon);
            if ((aPoly && bPoly) ||
                (aPoly && bSlope) || (bPoly && aSlope)) {
                float pushX, pushY;
                if (SatPolyPoly(a, b, pushX, pushY)) {
                    dx = pushX; dy = pushY;
                }
                return;
            }
            if (aPoly && b.kind == ColliderKind::Box) {
                float pushX, pushY;
                if (SatTriBox(a, b, pushX, pushY)) {
                    dx = -pushX; dy = -pushY;
                }
                return;
            }
            if (bPoly && a.kind == ColliderKind::Box) {
                float pushX, pushY;
                if (SatTriBox(b, a, pushX, pushY)) {
                    dx = pushX; dy = pushY;
                }
                return;
            }
            if (aPoly && b.kind == ColliderKind::Circle) {
                float pushX, pushY;
                SlopeCircleMtv(a, b, pushX, pushY);
                dx = -pushX; dy = -pushY;
                return;
            }
            if (bPoly && a.kind == ColliderKind::Circle) {
                float pushX, pushY;
                SlopeCircleMtv(b, a, pushX, pushY);
                dx = pushX; dy = pushY;
                return;
            }
            if (aSlope && b.kind == ColliderKind::Circle) {
                float pushX, pushY;
                SlopeCircleMtv(a, b, pushX, pushY);
                // Circle gets pushed by +push; for the function
                // to push `a` out of `b` we negate, so Resolve's
                // -dx/-dy for b matches the circle's push.
                dx = -pushX; dy = -pushY;
                return;
            }
            if (bSlope && a.kind == ColliderKind::Circle) {
                float pushX, pushY;
                SlopeCircleMtv(b, a, pushX, pushY);
                dx = pushX; dy = pushY;
                return;
            }
            if (aSlope && b.kind == ColliderKind::Box) {
                // SatTriBox returns MTV pushing box out of
                // triangle. We want `a` (slope) moved away
                // from `b` (box) -- flip sign.
                float pushX, pushY;
                if (SatTriBox(a, b, pushX, pushY)) {
                    dx = -pushX; dy = -pushY;
                }
                return;
            }
            if (bSlope && a.kind == ColliderKind::Box) {
                float pushX, pushY;
                if (SatTriBox(b, a, pushX, pushY)) {
                    dx = pushX; dy = pushY;
                }
                return;
            }
            if (a.kind == ColliderKind::Box &&
                b.kind == ColliderKind::Box) {
                // SAT does the orientation work; the result is
                // already an MTV pushing A out of B.
                SatBoxBox(a, b, dx, dy);
                return;
            }
            if (a.kind == ColliderKind::Circle &&
                b.kind == ColliderKind::Circle) {
                const float ex = a.cx - b.cx;
                const float ey = a.cy - b.cy;
                const float dist = std::sqrt(ex * ex + ey * ey);
                const float pen  = (a.radius + b.radius) - dist;
                if (dist < 1e-6f) { dx = pen; return; }
                dx = ex / dist * pen;
                dy = ey / dist * pen;
                return;
            }
            // Mixed: M7c13 routes the circle into the box's
            // *local* frame so the M7c10 closest-point clamp
            // works on the rotated box as if it were axis-
            // aligned. The push vector then rotates back into
            // world space.
            const bool aIsBox = (a.kind == ColliderKind::Box);
            const ColliderInfo& box = aIsBox ? a : b;
            const ColliderInfo& cir = aIsBox ? b : a;
            // Circle centre in box-local coordinates: dot with
            // each local axis. Box's X axis is (cosR, sinR);
            // Y axis is (-sinR, cosR).
            const float dxw = cir.cx - box.cx;
            const float dyw = cir.cy - box.cy;
            const float lx = box.cosR * dxw + box.sinR * dyw;
            const float ly = -box.sinR * dxw + box.cosR * dyw;
            const float cx2 = std::clamp(lx, -box.halfX, box.halfX);
            const float cy2 = std::clamp(ly, -box.halfY, box.halfY);
            float ex = lx - cx2;
            float ey = ly - cy2;
            float dist = std::sqrt(ex * ex + ey * ey);
            float pushLX = 0.0f, pushLY = 0.0f;
            if (dist < 1e-6f) {
                // Circle centre deep inside the (rotated) box.
                // Score each face by the velocity-relative
                // entry direction, in local frame.
                const float relVxW = cir.vx - box.vx;
                const float relVyW = cir.vy - box.vy;
                const float relVx =  box.cosR * relVxW + box.sinR * relVyW;
                const float relVy = -box.sinR * relVxW + box.cosR * relVyW;
                const float toLeft   = lx + box.halfX;
                const float toRight  = box.halfX - lx;
                const float toBottom = ly + box.halfY;
                const float toTop    = box.halfY - ly;
                struct Face { float nx, ny, depth; };
                const Face faces[4] = {
                    { -1.0f,  0.0f, toLeft   + cir.radius },
                    {  1.0f,  0.0f, toRight  + cir.radius },
                    {  0.0f, -1.0f, toBottom + cir.radius },
                    {  0.0f,  1.0f, toTop    + cir.radius },
                };
                int bestIdx = 0;
                float bestScore = -1.0f;
                for (int i = 0; i < 4; ++i) {
                    const float score = -(relVx * faces[i].nx +
                                          relVy * faces[i].ny);
                    if (score > bestScore) {
                        bestScore = score;
                        bestIdx = i;
                    }
                }
                if (bestScore <= 0.0f) {
                    float bestDepth = faces[0].depth;
                    bestIdx = 0;
                    for (int i = 1; i < 4; ++i) {
                        if (faces[i].depth < bestDepth) {
                            bestDepth = faces[i].depth;
                            bestIdx = i;
                        }
                    }
                }
                pushLX = faces[bestIdx].nx * faces[bestIdx].depth;
                pushLY = faces[bestIdx].ny * faces[bestIdx].depth;
            } else {
                const float pen = cir.radius - dist;
                pushLX = ex / dist * pen;
                pushLY = ey / dist * pen;
            }
            // Rotate the local-frame push back to world.
            const float pushX =  box.cosR * pushLX - box.sinR * pushLY;
            const float pushY =  box.sinR * pushLX + box.cosR * pushLY;
            // mtv we built pushes the circle out of the box. If
            // A is the circle the result is direct; otherwise
            // flip the sign.
            if (aIsBox) {
                dx = -pushX; dy = -pushY;
            } else {
                dx =  pushX; dy =  pushY;
            }
            };  // testPair

            const bool aCap = (a.kind == ColliderKind::Capsule);
            const bool bCap = (b.kind == ColliderKind::Capsule);
            const bool aEdge = (a.kind == ColliderKind::Edge);
            const bool bEdge = (b.kind == ColliderKind::Edge);
            if (!aCap && !bCap && !aEdge && !bEdge) {
                testPair(a, b, dx, dy); return;
            }
            // M7c32 / M7c27: unified decomposition fan-out.
            // Each side expands into a small list of sub-
            // ColliderInfos; the MTV is the sub-pair with the
            // largest push depth.
            std::vector<ColliderInfo> aSubsCap(3), bSubsCap(3);
            std::vector<ColliderInfo> aSubsEdge, bSubsEdge;
            const ColliderInfo* ap = &a;
            int aN = 1;
            if (aCap) {
                CapsuleDecompose(a, aSubsCap.data());
                ap = aSubsCap.data(); aN = 3;
            } else if (aEdge) {
                EdgeDecompose(a, aSubsEdge);
                ap = aSubsEdge.data();
                aN = int(aSubsEdge.size());
            }
            const ColliderInfo* bp = &b;
            int bN = 1;
            if (bCap) {
                CapsuleDecompose(b, bSubsCap.data());
                bp = bSubsCap.data(); bN = 3;
            } else if (bEdge) {
                EdgeDecompose(b, bSubsEdge);
                bp = bSubsEdge.data();
                bN = int(bSubsEdge.size());
            }
            float bestLenSq = -1.0f;
            float bestDx = 0.0f, bestDy = 0.0f;
            for (int i = 0; i < aN; ++i) {
                for (int j = 0; j < bN; ++j) {
                    float subDx = 0.0f, subDy = 0.0f;
                    testPair(ap[i], bp[j], subDx, subDy);
                    const float lenSq = subDx * subDx + subDy * subDy;
                    if (lenSq > bestLenSq) {
                        bestLenSq = lenSq;
                        bestDx = subDx;
                        bestDy = subDy;
                    }
                }
            }
            dx = bestDx; dy = bestDy;
        };

        // Per-shape overlap. Box-vs-box runs the M7c13 SAT
        // pass; circle-vs-circle compares centre distance to
        // summed radii; box-vs-circle uses the same local-frame
        // clamp the rotated MTV path uses.
        // M7c22: slope-vs-circle helper. Projects the circle
        // centre onto the slope's hypotenuse segment, clamps t
        // to [0, 1] so a ball that drifts past the cell's
        // corners doesn't fake-contact the infinite line, and
        // returns true when the closest-point distance is less
        // than the circle radius. The outward normal points
        // toward the unfilled triangle.
        auto SlopeCircleOverlap = [&PolyCircleClosest](
                const ColliderInfo& slope,
                const ColliderInfo& circ) {
            float pcx, pcy;
            bool inside;
            PolyCircleClosest(slope, circ, pcx, pcy, inside);
            if (inside) return true;
            const float ddx = circ.cx - pcx;
            const float ddy = circ.cy - pcy;
            return ddx * ddx + ddy * ddy <=
                   circ.radius * circ.radius;
        };
        auto Overlap = [&SatBoxBox, &SlopeCircleOverlap, &SatTriBox,
                        &CapsuleDecompose, &EdgeDecompose, &SatPolyPoly]
                       (const ColliderInfo& a, const ColliderInfo& b) {
            // Inner pair test -- handles every shape *except*
            // Capsule. M7c27 wraps it with a capsule decomp
            // pass so the same body of code keeps serving the
            // non-capsule pairs unchanged.
            auto testPair = [&](const ColliderInfo& a,
                                const ColliderInfo& b) -> bool {
            const bool aSlope =
                a.slope != ColliderInfo::SlopeShape::None;
            const bool bSlope =
                b.slope != ColliderInfo::SlopeShape::None;
            const bool aPoly = (a.kind == ColliderKind::Polygon);
            const bool bPoly = (b.kind == ColliderKind::Polygon);
            // M7c28: polygon side reuses the M7c25 helpers --
            // SatPolyBox + SlopeCircleOverlap already loop over
            // PolyVertices, which now returns polygon verts
            // directly when info.kind == Polygon.
            if ((aPoly && bPoly) ||
                (aPoly && bSlope) || (bPoly && aSlope)) {
                float dxIgn = 0.0f, dyIgn = 0.0f;
                return SatPolyPoly(a, b, dxIgn, dyIgn);
            }
            if (aPoly && b.kind == ColliderKind::Box) {
                float dxIgn = 0.0f, dyIgn = 0.0f;
                return SatTriBox(a, b, dxIgn, dyIgn);
            }
            if (bPoly && a.kind == ColliderKind::Box) {
                float dxIgn = 0.0f, dyIgn = 0.0f;
                return SatTriBox(b, a, dxIgn, dyIgn);
            }
            if (aPoly && b.kind == ColliderKind::Circle) {
                return SlopeCircleOverlap(a, b);
            }
            if (bPoly && a.kind == ColliderKind::Circle) {
                return SlopeCircleOverlap(b, a);
            }
            if (aSlope && b.kind == ColliderKind::Circle) {
                return SlopeCircleOverlap(a, b);
            }
            if (bSlope && a.kind == ColliderKind::Circle) {
                return SlopeCircleOverlap(b, a);
            }
            if (aSlope && b.kind == ColliderKind::Box) {
                float dxIgn = 0.0f, dyIgn = 0.0f;
                return SatTriBox(a, b, dxIgn, dyIgn);
            }
            if (bSlope && a.kind == ColliderKind::Box) {
                float dxIgn = 0.0f, dyIgn = 0.0f;
                return SatTriBox(b, a, dxIgn, dyIgn);
            }
            if (a.kind == ColliderKind::Box &&
                b.kind == ColliderKind::Box) {
                float dxIgn = 0.0f, dyIgn = 0.0f;
                return SatBoxBox(a, b, dxIgn, dyIgn);
            }
            if (a.kind == ColliderKind::Circle &&
                b.kind == ColliderKind::Circle) {
                const float dx = a.cx - b.cx;
                const float dy = a.cy - b.cy;
                const float r  = a.radius + b.radius;
                return dx * dx + dy * dy <= r * r;
            }
            const ColliderInfo& box =
                a.kind == ColliderKind::Box ? a : b;
            const ColliderInfo& circ =
                a.kind == ColliderKind::Box ? b : a;
            const float dxw = circ.cx - box.cx;
            const float dyw = circ.cy - box.cy;
            const float lx =  box.cosR * dxw + box.sinR * dyw;
            const float ly = -box.sinR * dxw + box.cosR * dyw;
            const float cx2 = std::clamp(lx, -box.halfX, box.halfX);
            const float cy2 = std::clamp(ly, -box.halfY, box.halfY);
            const float dx  = lx - cx2;
            const float dy  = ly - cy2;
            return dx * dx + dy * dy <= circ.radius * circ.radius;
            };  // testPair

            // M7c27 / M7c32 decomposition: Capsule expands to
            // 3 sub-shapes; Edge expands to N segment-OBBs.
            // Either side that needs decomposition routes
            // through the fan-out loop; everything else falls
            // through to testPair directly.
            const bool aCap  = (a.kind == ColliderKind::Capsule);
            const bool bCap  = (b.kind == ColliderKind::Capsule);
            const bool aEdge = (a.kind == ColliderKind::Edge);
            const bool bEdge = (b.kind == ColliderKind::Edge);
            if (!aCap && !bCap && !aEdge && !bEdge) {
                return testPair(a, b);
            }
            std::vector<ColliderInfo> aSubsCap(3), bSubsCap(3);
            std::vector<ColliderInfo> aSubsEdge, bSubsEdge;
            const ColliderInfo* ap = &a;
            int aN = 1;
            if (aCap) {
                CapsuleDecompose(a, aSubsCap.data());
                ap = aSubsCap.data(); aN = 3;
            } else if (aEdge) {
                EdgeDecompose(a, aSubsEdge);
                ap = aSubsEdge.data();
                aN = int(aSubsEdge.size());
            }
            const ColliderInfo* bp = &b;
            int bN = 1;
            if (bCap) {
                CapsuleDecompose(b, bSubsCap.data());
                bp = bSubsCap.data(); bN = 3;
            } else if (bEdge) {
                EdgeDecompose(b, bSubsEdge);
                bp = bSubsEdge.data();
                bN = int(bSubsEdge.size());
            }
            for (int i = 0; i < aN; ++i) {
                for (int j = 0; j < bN; ++j) {
                    if (testPair(ap[i], bp[j])) return true;
                }
            }
            return false;
        };
        // O(n^2) pair check. Fine for editor-scale entity counts;
        // a spatial grid lands when a real game pushes the count
        // past where this becomes hot.
        std::set<std::pair<SceneEditor::EntityId,
                           SceneEditor::EntityId>> nowPairs;
        for (size_t i = 0; i < infos.size(); ++i) {
            for (size_t j = i + 1; j < infos.size(); ++j) {
                const auto& a = infos[i];
                const auto& b = infos[j];
                // Bounding AABB cull first so the per-shape test
                // only runs on candidates that already share
                // screen space.
                if (a.maxX < b.minX || b.maxX < a.minX ||
                    a.maxY < b.minY || b.maxY < a.minY) continue;
                // M7c11: layer matrix filter. Pairs whose two
                // layers are un-ticked in the Project's
                // collision matrix never generate Enter / Stay
                // / Exit / push-out -- they pass right through
                // each other.
                if (!m_project.CollisionLayersCollide(a.layer, b.layer))
                    continue;
                if (!Overlap(a, b)) continue;
                // M7c21: OneWay tiles only block when the MTV
                // would push the other body upward. A body
                // jumping up from below the platform produces
                // a downward MTV (and a horizontal hit gives
                // ~zero vertical), so we skip the pair entirely
                // -- no contact events fire + no push-out
                // applies. Triggers ignore oneWay because the
                // trigger path doesn't push.
                if (a.oneWay || b.oneWay) {
                    float dx = 0.0f, dy = 0.0f;
                    ComputeMTV(a, b, dx, dy);
                    // ComputeMTV returns the MTV that moves `a`
                    // away from `b`. We want the MTV applied to
                    // the *other* (non-tile) side -- the moving
                    // body -- to be upward (+Y).
                    const float bodyMtvY = a.oneWay ? -dy : dy;
                    if (bodyMtvY <= 1e-4f) continue;
                }
                const auto key =
                    a.id < b.id
                        ? std::pair<SceneEditor::EntityId,
                                    SceneEditor::EntityId>{a.id, b.id}
                        : std::pair<SceneEditor::EntityId,
                                    SceneEditor::EntityId>{b.id, a.id};
                nowPairs.insert(key);
                const bool wasContact =
                    m_collisionPairs.find(key) != m_collisionPairs.end();
                // Solid contact when neither collider is a
                // trigger. Solid pairs push each other apart by
                // half the MTV + fire OnCollision events; the
                // rest fire OnTrigger like the M7c1/c3 path.
                const bool isSolid = !a.isTrigger && !b.isTrigger;
                if (isSolid) {
                    float dx = 0.0f, dy = 0.0f;
                    ComputeMTV(a, b, dx, dy);
                    SceneEditor::Entity* ea = m_scene.Get(a.id);
                    SceneEditor::Entity* eb = m_scene.Get(b.id);
                    auto* taC = ea
                        ? ea->GetComponent<SceneEditor::TransformComponent>()
                        : nullptr;
                    auto* tbC = eb
                        ? eb->GetComponent<SceneEditor::TransformComponent>()
                        : nullptr;
                    // M7c10: collision response now reads
                    // mass / restitution / friction off each
                    // dynamic side. MTV splits by inverse mass
                    // (lighter body moves more), the inward
                    // normal velocity component reflects by the
                    // combined restitution (max of both), and
                    // the tangent component damps by the
                    // combined friction (max). Without a
                    // Rigidbody2D the body is treated as
                    // infinite mass / static (matches M7c7).
                    auto* rbA = ea
                        ? ea->GetComponent<SceneEditor::Rigidbody2DComponent>()
                        : nullptr;
                    auto* rbB = eb
                        ? eb->GetComponent<SceneEditor::Rigidbody2DComponent>()
                        : nullptr;
                    const float invA = rbA && rbA->mass > 0.0f
                        ? 1.0f / rbA->mass : 0.0f;
                    const float invB = rbB && rbB->mass > 0.0f
                        ? 1.0f / rbB->mass : 0.0f;
                    const float invSum = invA + invB;
                    float aShare = 0.0f, bShare = 0.0f;
                    if (invSum > 0.0f) {
                        aShare = invA / invSum;
                        bShare = invB / invSum;
                    }
                    const float e =
                        std::max(rbA ? rbA->restitution : 0.0f,
                                 rbB ? rbB->restitution : 0.0f);
                    const float mu =
                        std::max(rbA ? rbA->friction : 0.0f,
                                 rbB ? rbB->friction : 0.0f);
                    const float mag = std::sqrt(dx * dx + dy * dy);
                    const float nx = mag > 1e-6f ? dx / mag : 0.0f;
                    const float ny = mag > 1e-6f ? dy / mag : 0.0f;
                    auto Resolve = [&](SceneEditor::TransformComponent* tr,
                                       SceneEditor::Rigidbody2DComponent* rb,
                                       float share,
                                       float pushX, float pushY) {
                        if (!tr || share <= 0.0f) return;
                        tr->position.x += pushX * share;
                        tr->position.y += pushY * share;
                        if (!rb || mag < 1e-6f) return;
                        // Normal points in the same direction as
                        // pushX/pushY (away from the other body).
                        const float pnx = pushX / mag;
                        const float pny = pushY / mag;
                        const float vn = rb->velocity.x * pnx +
                                         rb->velocity.y * pny;
                        if (vn < 0.0f) {
                            // Inward -- bounce by (1 + e) so the
                            // normal component flips from vn to
                            // -e*vn.
                            const float jn = -(1.0f + e) * vn;
                            rb->velocity.x += jn * pnx;
                            rb->velocity.y += jn * pny;
                        }
                        // Tangential damping. Tangent direction
                        // is perpendicular to the normal.
                        const float tx = -pny;
                        const float ty =  pnx;
                        const float vt = rb->velocity.x * tx +
                                         rb->velocity.y * ty;
                        rb->velocity.x -= vt * mu * tx;
                        rb->velocity.y -= vt * mu * ty;
                    };
                    Resolve(taC, rbA, aShare,  dx,  dy);
                    Resolve(tbC, rbB, bShare, -dx, -dy);
                    if (!wasContact) {
                        if (a.hasScript) m_scriptHost.CallOnCollisionEnter(
                            a.name, b.name, a.entryFunction);
                        if (b.hasScript) m_scriptHost.CallOnCollisionEnter(
                            b.name, a.name, b.entryFunction);
                    } else {
                        if (a.hasScript) m_scriptHost.CallOnCollisionStay(
                            a.name, b.name, a.entryFunction);
                        if (b.hasScript) m_scriptHost.CallOnCollisionStay(
                            b.name, a.name, b.entryFunction);
                    }
                } else if (!wasContact) {
                    if (a.hasScript) {
                        m_scriptHost.CallOnTriggerEnter(
                            a.name, b.name, a.entryFunction);
                    }
                    if (b.hasScript) {
                        m_scriptHost.CallOnTriggerEnter(
                            b.name, a.name, b.entryFunction);
                    }
                } else {
                    if (a.hasScript) {
                        m_scriptHost.CallOnTriggerStay(
                            a.name, b.name, a.entryFunction);
                    }
                    if (b.hasScript) {
                        m_scriptHost.CallOnTriggerStay(
                            b.name, a.name, b.entryFunction);
                    }
                }
            }
        }
        // Ended contacts -- pairs in m_collisionPairs but not in
        // nowPairs. Fire the matching Exit dispatcher; we don't
        // have to re-look-up isTrigger because the runtime
        // intent doesn't change between Enter / Stay / Exit
        // edges. Default to OnTriggerExit when either side is a
        // trigger or when we can't tell; only the
        // both-non-trigger case routes to OnCollisionExit.
        for (const auto& key : m_collisionPairs) {
            if (nowPairs.count(key)) continue;
            SceneEditor::Entity* ea = m_scene.Get(key.first);
            SceneEditor::Entity* eb = m_scene.Get(key.second);
            const auto* aBc = ea
                ? ea->GetComponent<SceneEditor::BoxColliderComponent>()
                : nullptr;
            const auto* aCc = ea
                ? ea->GetComponent<SceneEditor::CircleColliderComponent>()
                : nullptr;
            const auto* bBc = eb
                ? eb->GetComponent<SceneEditor::BoxColliderComponent>()
                : nullptr;
            const auto* bCc = eb
                ? eb->GetComponent<SceneEditor::CircleColliderComponent>()
                : nullptr;
            const bool aTrigger =
                (aBc && aBc->isTrigger) || (aCc && aCc->isTrigger);
            const bool bTrigger =
                (bBc && bBc->isTrigger) || (bCc && bCc->isTrigger);
            // M7c18: synthetic per-tile colliders are always
            // solid + don't carry triggers, so a real <-> cell
            // pair routes through OnCollisionExit even when one
            // side has no Entity to look up.
            auto SyntheticName = [&](SceneEditor::EntityId id) {
                auto it = m_syntheticColliderNames.find(id);
                return it != m_syntheticColliderNames.end()
                    ? it->second : std::string{};
            };
            const bool aSyn = !ea &&
                m_syntheticColliderNames.count(key.first);
            const bool bSyn = !eb &&
                m_syntheticColliderNames.count(key.second);
            const bool wasSolid = (ea || aSyn) && (eb || bSyn) &&
                !aTrigger && !bTrigger;
            if (ea) {
                auto* sc =
                    ea->GetComponent<SceneEditor::ScriptComponent>();
                if (sc) {
                    const std::string otherName = eb
                        ? eb->name : SyntheticName(key.second);
                    if (wasSolid) {
                        m_scriptHost.CallOnCollisionExit(
                            ea->name, otherName, sc->entryFunction);
                    } else {
                        m_scriptHost.CallOnTriggerExit(
                            ea->name, otherName, sc->entryFunction);
                    }
                }
            }
            if (eb) {
                auto* sc =
                    eb->GetComponent<SceneEditor::ScriptComponent>();
                if (sc) {
                    const std::string otherName = ea
                        ? ea->name : SyntheticName(key.first);
                    if (wasSolid) {
                        m_scriptHost.CallOnCollisionExit(
                            eb->name, otherName, sc->entryFunction);
                    } else {
                        m_scriptHost.CallOnTriggerExit(
                            eb->name, otherName, sc->entryFunction);
                    }
                }
            }
        }
        m_collisionPairs = std::move(nowPairs);
    }

    // Polls disk mtimes on every hot-reloadable asset + dispatches
    // the right reload path when one changes. Runs independently
    // of m_animPlaying so an external edit during a paused scene
    // still propagates the moment the user unpauses. First
    // observation just seeds the cache so the initial project-
    // open sweep doesn't double-load anything the M6c3 auto-
    // loader already handled.
    void TickAssetFileWatcher(float dt) {
        if (!m_project.IsOpen()) return;
        m_assetWatchAccum += dt;
        if (m_assetWatchAccum < kAssetWatchInterval) return;
        m_assetWatchAccum = 0.0f;
        // M11c4 structural-change check. If the on-disk file count
        // changed since the last poll, run a Rescan first -- the
        // per-entry mtime loop below only sees files already in
        // m_assetDb.Entries(), so a newly-dropped texture would
        // otherwise stay invisible until the user clicks Refresh.
        const size_t diskCount = m_assetDb.CountAssetFilesOnDisk();
        if (diskCount != m_assetWatchLastCount) {
            m_assetWatchLastCount = diskCount;
            m_assetDb.Rescan();
            m_scriptHost.AppendLine(
                "[watcher] Assets/ structural change -> rescan");
        }
        for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
            if (!IsHotReloadableType(a.typeName)) continue;
            const std::string abs =
                m_assetDb.AssetsRoot() + "/" + a.relPath;
            std::error_code ec;
            auto mtime = std::filesystem::last_write_time(abs, ec);
            if (ec) continue;
            auto it = m_assetMTimes.find(abs);
            if (it == m_assetMTimes.end()) {
                m_assetMTimes[abs] = mtime;
                continue;
            }
            if (it->second == mtime) continue;
            it->second = mtime;
            m_scriptHost.AppendLine(
                "[watcher] reloading " + a.relPath);
            // Dispatch to the asset-type-specific reload path.
            // Script files re-run through the Lua interpreter;
            // everything else evicts the AssetDatabase cache so
            // the next access path (animator tick, sprite draw,
            // tilemap renderer) re-reads from disk.
            if (a.typeName == "Script") {
                m_scriptHost.LoadFile(abs, a.relPath);
            } else if (a.typeName == "Animation") {
                m_assetDb.EvictAnimation(a.guid);
            } else if (a.typeName == "AnimatorController") {
                m_assetDb.EvictAnimatorController(a.guid);
            } else if (a.typeName == "Texture") {
                m_assetDb.EvictTexture(a.guid);
            } else if (a.typeName == "Tileset") {
                m_assetDb.EvictTileset(a.guid);
            } else if (a.typeName == "Autotile") {
                m_assetDb.EvictAutotile(a.guid);
            }
        }
    }

    // Walks every entity that carries a ScriptComponent + fires
    // its lifecycle hooks. OnStart runs exactly once per editor
    // session (guarded by the runtime `started` flag on
    // ScriptComponent); OnUpdate runs every frame the global play
    // toggle is on. Tickless when m_animPlaying is false so a
    // paused scene doesn't leak Update calls.
    void TickScripts(float dt) {
        if (!m_animPlaying) return;
        // Bump the host's running totals so the Lua `time` API
        // reads the right elapsed seconds + frame counter. Done
        // unconditionally here so scripts that read time.* from
        // their OnStart see consistent values on the first tick.
        m_scriptHost.Tick(dt);
        std::vector<SceneEditor::EntityId> ids;
        ids.reserve(64);
        CollectAllEntities(m_scene.Roots(), ids);
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* e = m_scene.Get(id);
            if (!e) continue;
            auto* sc = e->GetComponent<SceneEditor::ScriptComponent>();
            if (!sc) continue;
            // Edge detection on enabled. Fires before OnStart so
            // a freshly-loaded enabled entity sees the same
            // OnEnable -> OnStart -> OnUpdate ordering Unity ships
            // with.
            if (e->enabled != sc->lastSeenEnabled) {
                if (e->enabled) {
                    m_scriptHost.CallOnEnable(e->name, sc->entryFunction);
                } else {
                    m_scriptHost.CallOnDisable(e->name, sc->entryFunction);
                }
                sc->lastSeenEnabled = e->enabled;
            }
            if (!e->enabled) continue;
            if (!sc->started) {
                m_scriptHost.CallOnStart(e->name, sc->entryFunction);
                sc->started = true;
            }
            m_scriptHost.CallOnUpdate(e->name, dt, sc->entryFunction);
        }
    }

    // play toggle is on) and push the sampled frame's sub-sprite name
    // into the entity's SpriteRenderer. Tickless when m_animPlaying is
    // false so the viewport snaps to whatever frame was last sampled.
    // M8c7: AudioSource auto-play tick. Fires the
    // playOnStart clip on the rising edge of Play. The
    // `_started` flag on each source is reset whenever
    // Play exits (m_animPlaying false) so the next press
    // re-triggers exactly once.
    void TickAudioSources(float /*dt*/) {
        std::vector<SceneEditor::EntityId> ids;
        ids.reserve(64);
        CollectAllEntities(m_scene.Roots(), ids);
        if (!m_animPlaying) {
            for (SceneEditor::EntityId id : ids) {
                SceneEditor::Entity* e = m_scene.Get(id);
                if (!e) continue;
                auto* as = e->GetComponent<
                    SceneEditor::AudioSourceComponent>();
                if (!as) continue;
                as->_started = false;
            }
            return;
        }
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            auto* as = e->GetComponent<
                SceneEditor::AudioSourceComponent>();
            if (!as || as->_started) continue;
            as->_started = true;
            if (!as->playOnStart || !as->clip.IsValid()) {
                continue;
            }
            const auto* entry = m_assetDb.FindByGuid(as->clip);
            if (!entry || entry->typeName != "Audio") continue;
            const std::string& root = m_assetDb.AssetsRoot();
            std::string abs = root;
            if (!abs.empty() &&
                abs.back() != '/' && abs.back() != '\\') {
                abs.push_back('/');
            }
            abs.append(entry->relPath);
            if (as->loop) {
                SceneEditor::Audio::PlayMusic(
                    abs, as->volume, true);
                m_scriptHost.AppendLine(
                    std::string("[audio-source] ") + e->name +
                    " -> " + entry->relPath + " (loop)");
            } else if (as->is3D) {
                // M8c8 3D: find the active listener +
                // compute attenuation + pan against the
                // source's own Transform position.
                float lx = 0.0f, ly = 0.0f;
                bool foundListener = false;
                for (SceneEditor::EntityId lid : ids) {
                    SceneEditor::Entity* le = m_scene.Get(lid);
                    if (!le || !le->enabled) continue;
                    auto* al = le->GetComponent<
                        SceneEditor::AudioListenerComponent>();
                    if (!al) continue;
                    auto* lt = le->GetComponent<
                        SceneEditor::TransformComponent>();
                    if (!lt) continue;
                    lx = lt->position.x;
                    ly = lt->position.y;
                    foundListener = true;
                    break;
                }
                auto* st = e->GetComponent<
                    SceneEditor::TransformComponent>();
                if (!foundListener || !st) {
                    SceneEditor::Audio::Play(abs, as->volume);
                    m_scriptHost.AppendLine(
                        std::string("[audio-source] ") + e->name +
                        " -> " + entry->relPath +
                        " (3D requested, no listener -> 2D)");
                } else {
                    const float dx = st->position.x - lx;
                    const float dy = st->position.y - ly;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    float atten = 1.0f;
                    if (dist >= as->maxDistance) {
                        atten = 0.0f;
                    } else if (dist > as->minDistance &&
                                as->maxDistance > as->minDistance) {
                        atten =
                            (as->maxDistance - dist) /
                            (as->maxDistance - as->minDistance);
                    }
                    float pan = 0.0f;
                    if (as->maxDistance > 0.0f) {
                        pan = dx / as->maxDistance;
                        if (pan < -1.0f) pan = -1.0f;
                        if (pan >  1.0f) pan =  1.0f;
                    }
                    SceneEditor::Audio::PlayPositional(
                        abs, as->volume * atten, pan);
                    char log[256];
                    std::snprintf(log, sizeof(log),
                        "[audio-source] %s -> %s "
                        "(dist=%.2f atten=%.2f pan=%+.2f)",
                        e->name.c_str(),
                        entry->relPath.c_str(),
                        dist, atten, pan);
                    m_scriptHost.AppendLine(log);
                }
            } else {
                SceneEditor::Audio::Play(abs, as->volume);
                m_scriptHost.AppendLine(
                    std::string("[audio-source] ") + e->name +
                    " -> " + entry->relPath);
            }
        }
    }

    // M9c2 particle systems. Same play-mode gating as audio +
    // animators: when Play is off we reset every emitter back
    // to a clean state (zero live particles, fresh start
    // flag) so toggling Play on always restarts the burst.
    // When Play is on we emit, integrate gravity, and cull
    // dead particles in one swap-to-end pass. Cost is O(live)
    // per system; the fixed pool means no allocation past
    // the first frame.
    // M9c15 drain the script host's pending scene-load slot.
    // Called once per frame after TickScripts so a Lua callback
    // can request `scene.load("Scenes/Next.nscene")` and the
    // host swaps cleanly the next instant -- without the
    // entity iterator under the script callback exploding.
    void ConsumePendingSceneLoad() {
        // M10c3 first: advance an in-flight transition. The fade-
        // out phase ticks down here + when it hits zero, the
        // deferred scene load actually fires + the fade-in phase
        // begins. ConsumePendingSceneLoad runs once per OnRender
        // frame in both editor + standalone, so the timer is
        // driven by the same dt the rest of the scene sees.
        const float dt = ImGui::GetIO().DeltaTime;
        if (m_transitionState == TransitionState::FadingOut) {
            m_transitionElapsed += dt;
            if (m_transitionElapsed >= m_transitionFadeOut) {
                DoLoadSceneNow(m_transitionPendingPath);
                m_transitionPendingPath.clear();
                if (m_transitionFadeIn > 0.0f) {
                    m_transitionState   = TransitionState::FadingIn;
                    m_transitionElapsed = 0.0f;
                } else {
                    m_transitionState   = TransitionState::Idle;
                    m_transitionElapsed = 0.0f;
                }
                // Swallow any new pending request that piled up
                // mid-fade -- a script that calls scene.load twice
                // during the same fade would otherwise stack two
                // transitions. The freshly-loaded scene gets to run
                // a frame before we accept the next.
                return;
            }
            return;
        }
        if (m_transitionState == TransitionState::FadingIn) {
            m_transitionElapsed += dt;
            if (m_transitionElapsed >= m_transitionFadeIn) {
                m_transitionState   = TransitionState::Idle;
                m_transitionElapsed = 0.0f;
            }
            // Fall through so a *new* scene.load can queue up
            // mid-fade-in. The fade-in keeps drawing on top until
            // its timer ends.
        }

        if (!m_scriptHost.HasPendingSceneLoad()) return;
        float fadeOut = 0.0f, fadeIn = 0.0f, r = 0.0f, gC = 0.0f, b = 0.0f;
        m_scriptHost.ConsumePendingTransition(fadeOut, fadeIn, r, gC, b);
        std::string path = m_scriptHost.ConsumePendingSceneLoad();
        if (path.empty()) return;
        if (fadeOut > 0.0f) {
            // Defer the swap. Stash the path + fade-in params; the
            // next frame's tick above will run the load when the
            // fade-out timer expires.
            m_transitionPendingPath = std::move(path);
            m_transitionFadeOut     = fadeOut;
            m_transitionFadeIn      = fadeIn;
            m_transitionColorR      = r;
            m_transitionColorG      = gC;
            m_transitionColorB      = b;
            m_transitionState       = TransitionState::FadingOut;
            m_transitionElapsed     = 0.0f;
            return;
        }
        DoLoadSceneNow(path);
        if (fadeIn > 0.0f) {
            m_transitionFadeIn  = fadeIn;
            m_transitionColorR  = r;
            m_transitionColorG  = gC;
            m_transitionColorB  = b;
            m_transitionState   = TransitionState::FadingIn;
            m_transitionElapsed = 0.0f;
        }
    }

    void DoLoadSceneNow(const std::string& path) {
        if (path.empty()) return;
        NotifyAllScriptsForDestroy();
        if (m_scene.Load(path)) {
            m_currentScenePath = path;
            m_selection = SceneEditor::kInvalidEntityId;
            UndoClear();
            UpdateWindowTitle();
            NARAGAME_INFO("scene.load: %s", path.c_str());
        } else {
            NARAGAME_ERROR("scene.load: failed '%s'", path.c_str());
        }
    }

    // M10c3 transition overlay alpha for the current frame. 0 when
    // no transition is active, 1 at fade-out peak, then decays back
    // to 0 over fadeIn. Used by DrawTransitionOverlay.
    float CurrentTransitionAlpha() const {
        switch (m_transitionState) {
        case TransitionState::Idle: return 0.0f;
        case TransitionState::FadingOut:
            if (m_transitionFadeOut <= 0.0f) return 1.0f;
            return std::min(1.0f,
                m_transitionElapsed / m_transitionFadeOut);
        case TransitionState::FadingIn:
            if (m_transitionFadeIn <= 0.0f) return 0.0f;
            return std::max(0.0f, 1.0f -
                m_transitionElapsed / m_transitionFadeIn);
        }
        return 0.0f;
    }

    void DrawTransitionOverlay(ImDrawList* dl,
                                const ImVec2& panelMin,
                                const ImVec2& panelSize) {
        const float a = CurrentTransitionAlpha();
        if (a <= 0.0f) return;
        const ImU32 col = IM_COL32(
            int(m_transitionColorR * 255.0f + 0.5f),
            int(m_transitionColorG * 255.0f + 0.5f),
            int(m_transitionColorB * 255.0f + 0.5f),
            int(a * 255.0f + 0.5f));
        dl->AddRectFilled(panelMin,
                          ImVec2(panelMin.x + panelSize.x,
                                 panelMin.y + panelSize.y),
                          col);
    }

    void TickParticles(float dt) {
        std::vector<SceneEditor::EntityId> ids;
        ids.reserve(64);
        CollectAllEntities(m_scene.Roots(), ids);
        if (!m_animPlaying) {
            for (SceneEditor::EntityId id : ids) {
                SceneEditor::Entity* e = m_scene.Get(id);
                if (!e) continue;
                auto* ps = e->GetComponent<
                    SceneEditor::ParticleSystemComponent>();
                if (!ps) continue;
                ps->_started         = false;
                ps->_liveCount       = 0;
                ps->_emitAccum       = 0.0f;
                ps->_emittedTotal    = 0;
                ps->_elapsed         = 0.0f;
                ps->_burstNextIndex  = 0;
            }
            return;
        }
        // xorshift32: cheap PRNG that just needs a non-zero
        // seed. Lazy seeding from the entity id + a tick of
        // the frame counter keeps two systems in the same
        // scene out of phase.
        auto xs = [](std::uint32_t& s) -> std::uint32_t {
            if (s == 0) s = 0x9E3779B9u;
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            return s;
        };
        auto frand = [&](std::uint32_t& s, float lo, float hi) {
            const std::uint32_t r = xs(s);
            const float u = float(r) * (1.0f / 4294967296.0f);
            return lo + (hi - lo) * u;
        };
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            auto* ps = e->GetComponent<
                SceneEditor::ParticleSystemComponent>();
            if (!ps) continue;
            auto* tr = e->GetComponent<
                SceneEditor::TransformComponent>();
            if (!tr) continue;
            if (ps->maxParticles < 1) ps->maxParticles = 1;
            if (ps->maxParticles > 4096) ps->maxParticles = 4096;
            if (int(ps->_pool.size()) != ps->maxParticles) {
                ps->_pool.assign(size_t(ps->maxParticles),
                    SceneEditor::ParticleSystemComponent::Particle{});
                ps->_liveCount = 0;
            }
            if (!ps->_started) {
                ps->_started = true;
                if (!ps->playOnStart) continue;
                ps->_rng = std::uint32_t(id) * 2654435761u + 1u;
                ps->_elapsed        = 0.0f;
                ps->_burstNextIndex = 0;
            } else if (!ps->playOnStart && ps->_liveCount == 0 &&
                       ps->_emittedTotal == 0) {
                continue;
            }
            // M9c9 single-particle emit helper. Shared between
            // the continuous emissionRate path + the burst-
            // event path so both routes sample from the same
            // ranges + cap against the same _liveCount /
            // maxParticles guard. Returns true when a slot was
            // actually used.
            auto emitOne = [&]() {
                if (ps->_liveCount >= ps->maxParticles) return false;
                auto& p = ps->_pool[size_t(ps->_liveCount++)];
                ++ps->_emittedTotal;
                // M9c10 spawn offset by shape. Point spawns
                // at origin (legacy behaviour). Circle/Box
                // edge variants land on the boundary; volume
                // variants sample uniformly inside. Line
                // walks from (0,0) to shapeLineEnd.
                float ox = 0.0f, oy = 0.0f;
                using Sh = SceneEditor::ParticleEmissionShape;
                switch (ps->shape) {
                case Sh::Point: break;
                case Sh::CircleEdge: {
                    const float a = frand(ps->_rng, 0.0f, 1.0f) *
                        2.0f * 3.14159265358979323846f;
                    ox = std::cos(a) * ps->shapeRadius;
                    oy = std::sin(a) * ps->shapeRadius;
                    break;
                }
                case Sh::CircleVolume: {
                    const float a = frand(ps->_rng, 0.0f, 1.0f) *
                        2.0f * 3.14159265358979323846f;
                    // sqrt sample for uniform-by-area inside a
                    // disk (linear sampling would crowd the
                    // origin).
                    const float r = ps->shapeRadius *
                        std::sqrt(frand(ps->_rng, 0.0f, 1.0f));
                    ox = std::cos(a) * r;
                    oy = std::sin(a) * r;
                    break;
                }
                case Sh::BoxEdge: {
                    // Pick a perimeter parameter in [0, 1) and
                    // map to one of the 4 sides by perimeter
                    // fraction (so a tall, thin box favours its
                    // long sides).
                    const float w = ps->shapeBoxSize.x;
                    const float h = ps->shapeBoxSize.y;
                    const float perim = 2.0f * (w + h);
                    float t = frand(ps->_rng, 0.0f, 1.0f) * perim;
                    if (t < w) {
                        ox = -0.5f * w + t; oy = -0.5f * h;
                    } else if (t < w + h) {
                        ox =  0.5f * w; oy = -0.5f * h + (t - w);
                    } else if (t < 2.0f * w + h) {
                        ox =  0.5f * w - (t - w - h);
                        oy =  0.5f * h;
                    } else {
                        ox = -0.5f * w;
                        oy =  0.5f * h - (t - 2.0f * w - h);
                    }
                    break;
                }
                case Sh::BoxVolume: {
                    const float w = ps->shapeBoxSize.x;
                    const float h = ps->shapeBoxSize.y;
                    ox = frand(ps->_rng, -0.5f * w, 0.5f * w);
                    oy = frand(ps->_rng, -0.5f * h, 0.5f * h);
                    break;
                }
                case Sh::Line: {
                    const float t = frand(ps->_rng, 0.0f, 1.0f);
                    ox = ps->shapeLineEnd.x * t;
                    oy = ps->shapeLineEnd.y * t;
                    break;
                }
                }
                p.pos = { tr->position.x + ox,
                          tr->position.y + oy };
                const float ang = frand(ps->_rng,
                    ps->dirAngleMinDeg,
                    ps->dirAngleMaxDeg) *
                    (3.14159265358979323846f / 180.0f);
                const float spd = frand(ps->_rng,
                    ps->startSpeedMin, ps->startSpeedMax);
                p.vel = { std::cos(ang) * spd,
                          std::sin(ang) * spd };
                p.age      = 0.0f;
                p.lifetime = frand(ps->_rng,
                    ps->lifetimeMin,
                    std::max(ps->lifetimeMin,
                              ps->lifetimeMax));
                p.size     = frand(ps->_rng,
                    ps->startSizeMin, ps->startSizeMax);
                if (ps->endSizeMin < 0.0f ||
                    ps->endSizeMax < 0.0f) {
                    p.endSize = p.size;
                } else {
                    p.endSize = frand(ps->_rng,
                        ps->endSizeMin,
                        std::max(ps->endSizeMin,
                                  ps->endSizeMax));
                }
                constexpr float kDeg2Rad =
                    3.14159265358979323846f / 180.0f;
                p.rotation = frand(ps->_rng,
                    ps->startRotationMinDeg,
                    ps->startRotationMaxDeg) * kDeg2Rad;
                p.angularVel = frand(ps->_rng,
                    ps->angularSpeedMinDeg,
                    ps->angularSpeedMaxDeg) * kDeg2Rad;
                return true;
            };

            // M9c9 advance the cycle clock + fire bursts whose
            // time we've crossed. Two-stage loop: fire what
            // fits before the cycle end, wrap when loop=true
            // + duration elapsed, then re-check at the wrapped
            // elapsed so a 0-time burst right after a wrap still
            // lands. duration <= 0 disables the cycle so the
            // bursts list reduces to "fire once at startup".
            ps->_elapsed += dt;
            while (true) {
                while (ps->_burstNextIndex <
                            int(ps->bursts.size()) &&
                       ps->_elapsed >=
                            ps->bursts[size_t(
                                ps->_burstNextIndex)].time) {
                    const auto& b = ps->bursts[size_t(
                        ps->_burstNextIndex)];
                    const int n = (b.count < 0) ? 0 : b.count;
                    for (int k = 0; k < n; ++k) {
                        if (!emitOne()) break;
                    }
                    ++ps->_burstNextIndex;
                }
                if (ps->loop && ps->duration > 0.0f &&
                    ps->_elapsed >= ps->duration) {
                    ps->_elapsed -= ps->duration;
                    ps->_burstNextIndex = 0;
                    continue;
                }
                break;
            }

            // Emission step. Skip when looping has finished
            // a single non-loop batch.
            const bool emissionOpen =
                ps->loop ||
                ps->_emittedTotal < ps->maxParticles;
            if (emissionOpen && ps->emissionRate > 0.0f) {
                ps->_emitAccum += ps->emissionRate * dt;
                while (ps->_emitAccum >= 1.0f &&
                       (ps->loop ||
                        ps->_emittedTotal < ps->maxParticles)) {
                    ps->_emitAccum -= 1.0f;
                    if (!emitOne()) break;
                }
            }
            // Integrate + cull. Iterate while shrinking
            // _liveCount: dead particles get the last live
            // particle copied over them, no allocation.
            int i = 0;
            while (i < ps->_liveCount) {
                auto& p = ps->_pool[size_t(i)];
                p.vel.x += ps->gravity.x * dt;
                p.vel.y += ps->gravity.y * dt;
                p.pos.x += p.vel.x * dt;
                p.pos.y += p.vel.y * dt;
                p.rotation += p.angularVel * dt;
                p.age   += dt;
                if (p.age >= p.lifetime) {
                    p = ps->_pool[size_t(--ps->_liveCount)];
                } else {
                    ++i;
                }
            }
        }
    }

    // M10c2 splash screen overlay tick. Walks every enabled
    // SplashScreenComponent, advances its `_elapsed` while Play is
    // on (editor) or unconditionally (standalone), + fires the
    // nextScene scene.load exactly once when the timeline ends.
    // Reset semantics match TickParticles: a Play -> Pause cycle in
    // editor zeroes _elapsed so the next Play replays cleanly.
    void TickSplash(float dt) {
        std::vector<SceneEditor::EntityId> ids;
        ids.reserve(16);
        CollectAllEntities(m_scene.Roots(), ids);
        // Editor: paused -> rewind. Standalone has no pause concept.
        if (!m_animPlaying && !m_standalone) {
            for (SceneEditor::EntityId id : ids) {
                SceneEditor::Entity* e = m_scene.Get(id);
                if (!e) continue;
                if (auto* sp = e->GetComponent<
                        SceneEditor::SplashScreenComponent>()) {
                    sp->_elapsed    = 0.0f;
                    sp->_completed  = false;
                    sp->_everPlayed = false;
                }
            }
            return;
        }
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            auto* sp = e->GetComponent<
                SceneEditor::SplashScreenComponent>();
            if (!sp) continue;
            sp->_everPlayed = true;
            sp->_elapsed += dt;
            if (!sp->_completed && sp->_elapsed >= sp->TotalDuration()) {
                sp->_completed = true;
                if (!sp->nextScene.empty()) {
                    m_scriptHost.SetPendingSceneLoad(sp->nextScene);
                }
            }
        }
    }

    void TickAnimators(float dt) {
        if (!m_animPlaying) return;
        std::vector<SceneEditor::EntityId> ids;
        ids.reserve(64);
        CollectAllEntities(m_scene.Roots(), ids);
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            auto* an = e->GetComponent<SceneEditor::AnimatorComponent>();
            if (!an || !an->isPlaying) continue;

            // Controller branch wins over the standalone animation
            // field. The component's animation guid stays as a
            // fallback for entities that don't use a state machine.
            if (an->controller.IsValid()) {
                TickControllerAnimator(*an, dt, *e);
                continue;
            }
            SceneEditor::AnimationAsset* anim =
                m_assetDb.GetAnimation(an->animation);
            if (!anim || anim->frames.empty()) continue;
            const float prev = an->prevTime;
            an->currentTime += dt;
            FireEventsBetween(*e, *anim, prev, an->currentTime);
            an->prevTime = an->currentTime;
            const SceneEditor::AnimationFrame* fr =
                anim->Sample(an->currentTime);
            if (!fr) continue;
            if (auto* spr =
                    e->GetComponent<SceneEditor::SpriteRendererComponent>()) {
                if (spr->subSprite != fr->sprite) {
                    spr->subSprite = fr->sprite;
                }
            }
        }
    }

    // Arm an AnimatorComponent for a blended transition. Captures
    // the current SpriteRenderer alpha so we can restore it cleanly
    // when the blend completes; leaves currentState untouched
    // (TickTransitionBlend swaps it at the midpoint).
    void BeginTransitionBlend(SceneEditor::AnimatorComponent& an,
                              SceneEditor::Entity& e,
                              const SceneEditor::AnimatorTransition& tr) {
        an.transitionPending      = tr.toIdx;
        an.transitionDuration     = tr.duration;
        an.transitionElapsed      = 0.0f;
        an.transitionSwapped      = false;
        an.transitionTriggerParam.clear();
        an.transitionDesignAlpha  = 1.0f;
        if (auto* spr =
                e.GetComponent<SceneEditor::SpriteRendererComponent>()) {
            an.transitionDesignAlpha = spr->color.a;
        }
    }

    // Tick an in-flight transition blend: cross-fade the
    // SpriteRenderer's alpha, swap to the target state at the
    // midpoint, and clean up on completion. Returns true while the
    // blend is still in progress so the caller knows to skip new
    // transition evaluation.
    bool TickTransitionBlend(SceneEditor::AnimatorComponent& an,
                             float dt, SceneEditor::Entity& e) {
        if (an.transitionPending < 0) return false;
        if (an.transitionDuration <= 0.0f) {
            an.transitionPending      = -1;
            an.transitionSwapped      = false;
            an.transitionTriggerParam.clear();
            return false;
        }
        an.transitionElapsed += dt;
        const float t = std::clamp(
            an.transitionElapsed / an.transitionDuration, 0.0f, 1.0f);
        // V-shape fade: 1 at the edges, 0 at the midpoint, so the
        // user sees a clean fade-out / fade-in instead of an abrupt
        // pop on the swap.
        const float fade = std::abs(2.0f * t - 1.0f);
        if (auto* spr =
                e.GetComponent<SceneEditor::SpriteRendererComponent>()) {
            spr->color.a = an.transitionDesignAlpha * fade;
        }
        // Mid-point: swap the state so the second half plays the
        // target animation. Guard with transitionSwapped so a slow
        // tick that lands past 0.5 in one frame doesn't double-fire.
        if (!an.transitionSwapped && t >= 0.5f) {
            an.currentState     = an.transitionPending;
            an.currentTime      = 0.0f;
            an.prevTime         = 0.0f;
            an.transitionSwapped = true;
        }
        if (an.transitionElapsed >= an.transitionDuration) {
            // End: restore alpha + clear pending. Consume the
            // trigger now so the "armed" inspector indicator stays
            // visible through the full fade.
            if (auto* spr =
                    e.GetComponent<SceneEditor::SpriteRendererComponent>()) {
                spr->color.a = an.transitionDesignAlpha;
            }
            if (!an.transitionTriggerParam.empty()) {
                an.paramBools[an.transitionTriggerParam] = false;
                an.transitionTriggerParam.clear();
            }
            an.transitionPending      = -1;
            an.transitionElapsed      = 0.0f;
            an.transitionDuration     = 0.0f;
            an.transitionSwapped      = false;
            return false;
        }
        return true;
    }

    // Drive an AnimatorComponent through its controller. Picks the
    // initial state on the first tick, advances the current state's
    // animation, samples it onto the SpriteRenderer, and then walks
    // the transition list once per frame.
    void TickControllerAnimator(SceneEditor::AnimatorComponent& an,
                                float dt,
                                SceneEditor::Entity& e) {
        SceneEditor::AnimatorControllerAsset* ctrl =
            m_assetDb.GetAnimatorController(an.controller);
        if (!ctrl || ctrl->states.empty()) return;

        // Initialise on first run / out-of-range state. Parameter
        // maps are also bootstrapped from the controller's declared
        // defaults the first time we see this animator -- per type.
        if (an.currentState < 0 ||
            an.currentState >= int(ctrl->states.size())) {
            an.currentState = std::clamp(ctrl->defaultState, 0,
                                         int(ctrl->states.size()) - 1);
            an.currentTime  = 0.0f;
            for (const auto& p : ctrl->parameters) {
                using PT = SceneEditor::AnimatorParameter::Type;
                if (p.type == PT::Float) {
                    if (an.paramFloats.find(p.name) == an.paramFloats.end()) {
                        an.paramFloats[p.name] = p.defaultFloat;
                    }
                } else if (p.type == PT::Int) {
                    if (an.paramInts.find(p.name) == an.paramInts.end()) {
                        an.paramInts[p.name] = p.defaultInt;
                    }
                } else if (p.type == PT::Trigger) {
                    // Trigger always boots false; the user has to
                    // re-fire it explicitly through the inspector
                    // button each time.
                    if (an.paramBools.find(p.name) == an.paramBools.end()) {
                        an.paramBools[p.name] = false;
                    }
                } else if (p.type == PT::Vec2) {
                    if (an.paramVec2.find(p.name) == an.paramVec2.end()) {
                        an.paramVec2[p.name] = p.defaultVec2;
                    }
                } else if (p.type == PT::Color) {
                    if (an.paramColors.find(p.name) == an.paramColors.end()) {
                        an.paramColors[p.name] = p.defaultColor;
                    }
                } else if (p.type == PT::String) {
                    if (an.paramStrings.find(p.name) == an.paramStrings.end()) {
                        an.paramStrings[p.name] = p.defaultString;
                    }
                } else {
                    if (an.paramBools.find(p.name) == an.paramBools.end()) {
                        an.paramBools[p.name] = p.defaultBool;
                    }
                }
            }
        }

        // Tick any in-flight transition blend before the per-state
        // logic; the blend handler will swap currentState at the
        // midpoint so the per-state code automatically picks up the
        // new animation in the same frame.
        const bool inBlend = TickTransitionBlend(an, dt, e);

        const SceneEditor::AnimatorState& st = ctrl->states[size_t(an.currentState)];
        SceneEditor::AnimationAsset* anim = m_assetDb.GetAnimation(st.animation);
        if (anim && !anim->frames.empty()) {
            const float prev = an.prevTime;
            an.currentTime += dt * st.speed;
            FireEventsBetween(e, *anim, prev, an.currentTime);
            an.prevTime = an.currentTime;
            if (const SceneEditor::AnimationFrame* fr =
                    anim->Sample(an.currentTime)) {
                if (auto* spr =
                        e.GetComponent<SceneEditor::SpriteRendererComponent>()) {
                    if (spr->subSprite != fr->sprite) {
                        spr->subSprite = fr->sprite;
                    }
                }
            }
        }

        // Transition evaluation in declaration order. A transition
        // fires when its `fromIdx` matches the current state (or is
        // -1 = any), the named parameter is declared on the
        // controller, and the value-vs-target comparison passes for
        // the parameter's type. Skip the loop entirely while a blend
        // is still in flight so transitions can't stack.
        if (inBlend) return;
        for (const SceneEditor::AnimatorTransition& tr : ctrl->transitions) {
            if (tr.fromIdx >= 0 && tr.fromIdx != an.currentState) continue;
            if (tr.toIdx < 0 || tr.toIdx >= int(ctrl->states.size())) continue;
            // Exit-time gate: when the transition is timed, hold off
            // firing until the active animation has played past the
            // normalised threshold. Without an animation duration we
            // can't evaluate the gate, so timed transitions silently
            // wait.
            if (tr.hasExitTime) {
                if (!anim || anim->frames.empty()) continue;
                const float total = anim->TotalDuration();
                if (total <= 0.0f) continue;
                if (an.currentTime < tr.exitTime * total) continue;
            }
            // Empty param + hasExitTime is the pure auto-progress
            // pattern -- fires when the gate passes regardless of
            // parameters. Empty param + no exit time is a misconfig;
            // skip it the same way the M5c4 path did.
            if (tr.param.empty()) {
                if (!tr.hasExitTime) continue;
                if (tr.duration > 0.0f) {
                    BeginTransitionBlend(an, e, tr);
                } else {
                    an.currentState = tr.toIdx;
                    an.currentTime  = 0.0f;
                    an.prevTime     = 0.0f;
                }
                break;
            }
            const SceneEditor::AnimatorParameter* decl = nullptr;
            for (const auto& p : ctrl->parameters) {
                if (p.name == tr.param) { decl = &p; break; }
            }
            if (!decl) continue;
            using PT = SceneEditor::AnimatorParameter::Type;
            using OP = SceneEditor::AnimatorTransition::Op;
            auto cmp = [&](auto v, auto target) {
                switch (tr.op) {
                    case OP::Equals:       return v == target;
                    case OP::NotEqual:     return v != target;
                    case OP::Less:         return v <  target;
                    case OP::LessEqual:    return v <= target;
                    case OP::Greater:      return v >  target;
                    case OP::GreaterEqual: return v >= target;
                }
                return false;
            };
            bool fire = false;
            if (decl->type == PT::Float) {
                auto it = an.paramFloats.find(tr.param);
                const float v = (it != an.paramFloats.end())
                              ? it->second : decl->defaultFloat;
                fire = cmp(v, tr.valueFloat);
            } else if (decl->type == PT::Int) {
                auto it = an.paramInts.find(tr.param);
                const int v = (it != an.paramInts.end())
                            ? it->second : decl->defaultInt;
                fire = cmp(v, tr.valueInt);
            } else if (decl->type == PT::Vec2) {
                auto it = an.paramVec2.find(tr.param);
                const NaraGame::Vec2 v = (it != an.paramVec2.end())
                    ? it->second : decl->defaultVec2;
                const bool eq = (v.x == tr.valueVec2.x &&
                                 v.y == tr.valueVec2.y);
                fire = (tr.op == OP::NotEqual) ? !eq : eq;
            } else if (decl->type == PT::Color) {
                auto it = an.paramColors.find(tr.param);
                const NaraGame::Color v = (it != an.paramColors.end())
                    ? it->second : decl->defaultColor;
                const bool eq = (v.r == tr.valueColor.r &&
                                 v.g == tr.valueColor.g &&
                                 v.b == tr.valueColor.b &&
                                 v.a == tr.valueColor.a);
                fire = (tr.op == OP::NotEqual) ? !eq : eq;
            } else if (decl->type == PT::String) {
                auto it = an.paramStrings.find(tr.param);
                const std::string& v = (it != an.paramStrings.end())
                    ? it->second : decl->defaultString;
                const bool eq = (v == tr.valueString);
                fire = (tr.op == OP::NotEqual) ? !eq : eq;
            } else {
                // Bool + Trigger share the paramBools map. Bool only
                // supports == / != meaningfully; the other ops
                // degenerate to the equality check below. Triggers
                // get a default of false on lookup (matches the
                // bootstrap above).
                auto it = an.paramBools.find(tr.param);
                const bool baseline = (decl->type == PT::Trigger)
                    ? false : decl->defaultBool;
                const bool v = (it != an.paramBools.end())
                             ? it->second : baseline;
                if (tr.op == OP::NotEqual) fire = (v != tr.valueBool);
                else                       fire = (v == tr.valueBool);
            }
            if (!fire) continue;
            if (tr.duration > 0.0f) {
                BeginTransitionBlend(an, e, tr);
                // Defer trigger consumption to the blend end so the
                // inspector keeps showing the "armed" indicator
                // through the fade.
                if (decl->type == PT::Trigger) {
                    an.transitionTriggerParam = tr.param;
                }
            } else {
                an.currentState = tr.toIdx;
                an.currentTime  = 0.0f;
                an.prevTime     = 0.0f;
                if (decl->type == PT::Trigger) {
                    an.paramBools[tr.param] = false;
                }
            }
            break;
        }
    }

    // Editor-wide keyboard shortcuts. ImGui-driven so input that landed
    // in a text field falls through to the widget instead of triggering
    // a global undo.
    void HandleGlobalShortcuts() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) return;
        const bool ctrl  = io.KeyCtrl;
        const bool shift = io.KeyShift;
        if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Z)) DoUndo();
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y) ||
                     (shift && ImGui::IsKeyPressed(ImGuiKey_Z)))) {
            DoRedo();
        }
    }

    // --- Undo / redo -----------------------------------------------------
    //
    // Snapshot the active scene before a user action that mutates it.
    // Wipes the redo stack -- a fresh edit branches off the timeline.
    void UndoSnapshot() {
        m_undoStack.push_back(m_scene.SerializeToString());
        if (m_undoStack.size() > kUndoStackMax) {
            m_undoStack.erase(m_undoStack.begin());
        }
        m_redoStack.clear();
    }

    void UndoClear() {
        m_undoStack.clear();
        m_redoStack.clear();
    }

    void DoUndo() {
        if (m_undoStack.empty()) return;
        m_redoStack.push_back(m_scene.SerializeToString());
        if (m_redoStack.size() > kUndoStackMax) {
            m_redoStack.erase(m_redoStack.begin());
        }
        NotifyAllScriptsForDestroy();
        m_scene.DeserializeFromString(m_undoStack.back());
        m_undoStack.pop_back();
        m_scene.MarkDirty();
        AfterUndoStateChange();
    }

    void DoRedo() {
        if (m_redoStack.empty()) return;
        m_undoStack.push_back(m_scene.SerializeToString());
        if (m_undoStack.size() > kUndoStackMax) {
            m_undoStack.erase(m_undoStack.begin());
        }
        NotifyAllScriptsForDestroy();
        m_scene.DeserializeFromString(m_redoStack.back());
        m_redoStack.pop_back();
        m_scene.MarkDirty();
        AfterUndoStateChange();
    }

    // Selection + transient editor state can point at entities the
    // undone state no longer contains; clear so we don't hold a stale
    // id. Drag flags reset for the same reason.
    void AfterUndoStateChange() {
        if (m_selection != SceneEditor::kInvalidEntityId &&
            !m_scene.Get(m_selection)) {
            m_selection = SceneEditor::kInvalidEntityId;
        }
        m_gizmoActive = GizmoAxis::None;
        m_rectActive  = false;
        UpdateWindowTitle();
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
        ApplyProjectAudioMixer();
        ReloadAllScripts();
        UpdateWindowTitle();
    }

    // M8c12. Push the project's persisted master/SFX/music levels
    // into the live Audio engine. Called whenever a project is
    // opened (CLI flag, File > Open Project, New Project flow) so
    // the bus state always matches the JSON.
    void ApplyProjectAudioMixer() {
        SceneEditor::Audio::SetBusVolume(
            SceneEditor::Audio::Bus::Master,
            m_project.AudioMasterVolume());
        SceneEditor::Audio::SetBusVolume(
            SceneEditor::Audio::Bus::Sfx,
            m_project.AudioSfxVolume());
        SceneEditor::Audio::SetBusVolume(
            SceneEditor::Audio::Bus::Music,
            m_project.AudioMusicVolume());
        SceneEditor::Audio::SetReverbWet(
            m_project.AudioReverbWet());
    }

    // Walks every "Script" typed asset in the database + sends it
    // to the host. Used both at startup (after the initial scan)
    // and from the Edit > Reload Scripts menu when the user has
    // edited a .lua file outside the editor and wants the
    // interpreter to pick up the change without restarting.
    void ReloadAllScripts() {
        if (!m_assetDb.IsOpen()) return;
        size_t loaded = 0;
        for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
            if (a.typeName != "Script") continue;
            const std::string abs =
                m_assetDb.AssetsRoot() + "/" + a.relPath;
            if (m_scriptHost.LoadFile(abs, a.relPath)) ++loaded;
        }
        if (loaded == 0 && !m_assetDb.Entries().empty()) {
            // Silent on empty projects; tell the user when there
            // really were Script assets but every one failed to
            // load so they don't think the call did nothing.
            bool anyScripts = false;
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName == "Script") { anyScripts = true; break; }
            }
            if (anyScripts) {
                m_scriptHost.AppendLine(
                    "[lua] reload finished with errors -- "
                    "check the lines above");
            }
        }
    }

    void DoCloseProject() {
        if (!m_project.IsOpen()) return;
        NotifyAllScriptsForDestroy();
        m_project.Close();
        m_assetDb.Close();
        m_assetMTimes.clear();
        m_assetWatchLastCount = 0;
        m_scene = SceneEditor::Scene();
        m_currentScenePath.clear();
        m_selection = SceneEditor::kInvalidEntityId;
        UndoClear();
        UpdateWindowTitle();
    }

    // --- Scene flow -----------------------------------------------------

    std::string ScenesDir() const {
        return m_project.IsOpen() ? (m_project.AssetsDir() + "/Scenes")
                                  : std::string{};
    }

    void DoNewScene() {
        NotifyAllScriptsForDestroy();
        m_scene = SceneEditor::Scene();
        m_scene.SetName("Untitled");
        m_currentScenePath.clear();
        m_selection = SceneEditor::kInvalidEntityId;
        UndoClear();
        UpdateWindowTitle();
    }

    void DoOpenScene() {
        const std::string path = PickFileDialog(
            m_hwnd, CLSID_FileOpenDialog, L"Open Scene",
            L"NaraGame Scene (*.nscene)", L"*.nscene", L"nscene",
            ScenesDir());
        if (path.empty()) return;
        NotifyAllScriptsForDestroy();
        if (m_scene.Load(path)) {
            m_currentScenePath = path;
            m_selection = SceneEditor::kInvalidEntityId;
            UndoClear();
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

    void DoNewController() {
        const std::string animDir =
            m_project.IsOpen() ? (m_project.AssetsDir() + "/Animations")
                               : std::string{};
        if (!animDir.empty()) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(fs::path(animDir), ec);
        }
        const std::string path = PickFileDialog(
            m_hwnd, CLSID_FileSaveDialog, L"New Animator Controller",
            L"NaraGame Controller (*.ncontroller)",
            L"*.ncontroller", L"ncontroller",
            animDir);
        if (path.empty()) return;
        SceneEditor::AnimatorControllerAsset ctrl;
        if (!ctrl.SaveToFile(path)) return;
        m_assetDb.Rescan();
        if (const auto* entry = m_assetDb.FindByPath(RelToAssets(path))) {
            OpenControllerEditor(entry->guid);
        }
    }

    void DoNewAnimation() {
        const std::string animDir =
            m_project.IsOpen() ? (m_project.AssetsDir() + "/Animations")
                               : std::string{};
        // Ensure the folder exists for older projects that pre-date the
        // M5 default subfolder; the save dialog falls back to Assets/
        // anyway when this can't be created.
        if (!animDir.empty()) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(fs::path(animDir), ec);
        }
        const std::string path = PickFileDialog(
            m_hwnd, CLSID_FileSaveDialog, L"New Animation",
            L"NaraGame Animation (*.nanim)", L"*.nanim", L"nanim",
            animDir);
        if (path.empty()) return;
        SceneEditor::AnimationAsset an;
        if (!an.SaveToFile(path)) return;
        m_assetDb.Rescan();
        if (const auto* entry = m_assetDb.FindByPath(RelToAssets(path))) {
            OpenAnimationEditor(entry->guid);
        }
    }

    void DoNewTileset() {
        const std::string tilesetsDir =
            m_project.IsOpen() ? (m_project.AssetsDir() + "/Tilesets")
                               : std::string{};
        const std::string path = PickFileDialog(
            m_hwnd, CLSID_FileSaveDialog, L"New Tileset",
            L"NaraGame Tileset (*.ntileset)", L"*.ntileset", L"ntileset",
            tilesetsDir);
        if (path.empty()) return;
        SceneEditor::TilesetAsset ts;
        if (!ts.SaveToFile(path)) return;
        m_assetDb.Rescan();
        // Open the editor on the freshly created file so the user can
        // start picking its texture immediately.
        if (const auto* entry = m_assetDb.FindByPath(RelToAssets(path))) {
            OpenTilesetEditor(entry->guid);
        }
    }

    // M9c12 mark the current scene as the project's startup
    // scene. Stored relative to Assets/ so a Build Game copy
    // (which preserves the relative tree) lands on the same
    // file.
    void DoSetMainScene() {
        if (!m_project.IsOpen() || m_currentScenePath.empty()) return;
        const std::string rel = RelToAssets(m_currentScenePath);
        m_project.SetMainScenePath(rel);
        m_project.Save();
        NARAGAME_INFO("scene-editor: main scene set to %s",
                      rel.c_str());
    }

    // M9c12 / M9c13 build a shippable game folder.
    //
    // Layout (after a successful build):
    //   <dest>/game.exe          -- copy of scene-editor.exe
    //   <dest>/assets.pak        -- single-file bundle of Assets/
    //   <dest>/ProjectSettings/  -- recursive copy (kept loose
    //                               so the user can edit
    //                               post-ship volume / layer
    //                               defaults without re-
    //                               packing)
    //
    // Run `game.exe` directly: with no args it falls into the
    // standalone runtime path (CreateApplication auto-detects
    // either a loose project tree or an assets.pak in its
    // working directory + loads the project's mainScene). The
    // asset DB, audio mixer + every scene/script/audio/texture
    // path keeps working unmodified because the pak's TOC
    // preserves the same Assets/-relative tree the editor
    // wrote -- the runtime unpacks once into a temp folder +
    // points the cwd at it.
    void DoBuildGame() {
        if (!m_project.IsOpen()) return;
        const std::string dest =
            PickFolderDialog(m_hwnd, L"Choose build destination");
        if (dest.empty()) return;

        namespace fs = std::filesystem;
        std::error_code ec;

        // Refuse to dump into a non-empty folder so a stray
        // pick doesn't carpet-bomb the user's desktop.
        if (fs::exists(dest, ec) && fs::is_directory(dest, ec)) {
            if (fs::directory_iterator(dest, ec) !=
                fs::directory_iterator()) {
                NARAGAME_ERROR(
                    "Build Game: '%s' is not empty -- "
                    "refusing to overwrite.", dest.c_str());
                return;
            }
        } else {
            fs::create_directories(dest, ec);
        }

        // Locate this exe so we can drop a renamed copy of it
        // in the destination as `game.exe`. GetModuleFileNameW
        // is the canonical way + handles long paths better than
        // argv[0].
        wchar_t exePathW[MAX_PATH] = { 0 };
        if (!GetModuleFileNameW(nullptr, exePathW, MAX_PATH)) {
            NARAGAME_ERROR("Build Game: GetModuleFileName failed.");
            return;
        }
        const fs::path srcExe(exePathW);
        const fs::path destExe = fs::path(dest) / L"game.exe";
        fs::copy_file(srcExe, destExe,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            NARAGAME_ERROR("Build Game: cannot copy exe to '%s' (%s)",
                           destExe.string().c_str(),
                           ec.message().c_str());
            return;
        }

        // M9c13 pack the Assets/ tree into a single
        // assets.pak archive instead of copying it loose. The
        // pak's TOC stores forward-slash relative paths so
        // the runtime extractor can reconstruct the same
        // directory layout the loose-copy path used to
        // produce.
        const fs::path pakPath = fs::path(dest) / L"assets.pak";
        if (!SceneEditor::WritePak(pakPath.string(),
                                     m_project.AssetsDir())) {
            NARAGAME_ERROR(
                "Build Game: failed to write '%s'",
                pakPath.string().c_str());
            return;
        }

        // ProjectSettings stays loose -- it's small, the user
        // may want to tweak `mainScene` or layer presets
        // post-ship without re-running Build Game, and it
        // doesn't carry the binary blobs that justify packing.
        const auto copyOpts =
            fs::copy_options::recursive |
            fs::copy_options::overwrite_existing;
        fs::copy(m_project.SettingsDir(),
                  (fs::path(dest) / L"ProjectSettings").string(),
                  copyOpts, ec);
        if (ec) {
            NARAGAME_ERROR(
                "Build Game: failed to copy ProjectSettings (%s)",
                ec.message().c_str());
            return;
        }

        NARAGAME_INFO("Build Game: '%s' built to %s "
                      "(main scene = '%s')",
                      m_project.Name().c_str(),
                      dest.c_str(),
                      m_project.MainScenePath().empty()
                        ? "<unset -- empty scene>"
                        : m_project.MainScenePath().c_str());
    }

    // Strip the project's Assets/ prefix off `abs` so we can hand a
    // relative path to AssetDatabase::FindByPath.
    std::string RelToAssets(const std::string& abs) const {
        const std::string root = m_project.AssetsDir() + "/";
        if (abs.size() > root.size() &&
            abs.compare(0, root.size(), root) == 0) {
            return abs.substr(root.size());
        }
        return abs;
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
            if (ImGui::MenuItem("New Tileset...", nullptr, false, projOpen))
                DoNewTileset();
            if (ImGui::MenuItem("New Animation...", nullptr, false, projOpen))
                DoNewAnimation();
            if (ImGui::MenuItem("New Animator Controller...",
                                nullptr, false, projOpen))
                DoNewController();
            ImGui::Separator();
            if (ImGui::MenuItem("Set Current Scene as Main",
                                nullptr, false,
                                projOpen && !m_currentScenePath.empty())) {
                DoSetMainScene();
            }
            if (ImGui::MenuItem("Build Game...", nullptr, false,
                                projOpen)) {
                DoBuildGame();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) Close();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            const bool canU = !m_undoStack.empty();
            const bool canR = !m_redoStack.empty();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canU)) DoUndo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canR)) DoRedo();
            ImGui::Separator();
            if (ImGui::MenuItem("Reload Scripts", nullptr, false,
                                m_project.IsOpen())) {
                m_assetDb.Rescan();
                ReloadAllScripts();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Reset Layout")) m_resetLayout = true;
            if (ImGui::MenuItem("Sorting Layers...", nullptr, false,
                                m_project.IsOpen())) {
                m_openLayersModal = true;
            }
            if (ImGui::MenuItem("Collision Matrix...", nullptr, false,
                                m_project.IsOpen())) {
                m_openCollisionMatrixModal = true;
            }
            if (ImGui::MenuItem("Audio Mixer...", nullptr, false,
                                m_project.IsOpen())) {
                m_openAudioMixerModal = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Editor Preferences...")) {
                m_openEditorPrefsModal = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("ImGui Demo", nullptr, &m_showDemo);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // --- Sprite Editor --------------------------------------------------
    //
    // Floating editor opened by double-clicking a Texture in the Project
    // panel. Slices the texture into a regular grid (by cell size or by
    // count) and stores the resulting sub-sprite rects on the texture's
    // .meta file. The texture preview + grid overlay use the same ImGui
    // DrawList path the Viewport relies on.

    void OpenSpriteEditor(const SceneEditor::AssetGuid& guid) {
        m_spriteEditorAsset    = guid;
        m_spriteEditorOpen     = true;
        m_spriteEditorActivate = true;
    }

    void OpenControllerEditor(const SceneEditor::AssetGuid& guid) {
        m_controllerEditorAsset    = guid;
        m_controllerEditorOpen     = true;
        m_controllerEditorActivate = true;
        m_graphSelectedState       = -1;
        m_graphDragState           = -1;
        m_graphSelectedTransition  = -1;
        m_graphPendingFrom         = -1;
        m_graphContextNode         = -1;
        m_controllerEditorParamFilter[0] = '\0';
        m_controllerEditorParamTypeFilter = 0;
        m_controllerEditorTrFromFilter    = 0;
        m_controllerEditorTrToFilter      = 0;
        m_controllerEditorTrParamFilter   = 0;
    }

    // True when the Combo's "All" entry (idx 0) is picked or when
    // the parameter's type matches the selected entry. The combo's
    // entry order tracks the AnimatorParameter::Type ordinal, so a
    // filterIdx of N >= 1 maps to type ordinal N - 1.
    static bool ParamTypeMatchesFilter(
        SceneEditor::AnimatorParameter::Type t, int filterIdx) {
        if (filterIdx <= 0) return true;
        return int(t) == filterIdx - 1;
    }

    // Case-insensitive substring match used by the parameter
    // filters. Returns true for an empty filter so callers can
    // unconditionally skip rows that don't match.
    static bool ParamNameMatchesFilter(const std::string& name,
                                       const char* filter) {
        if (!filter || filter[0] == '\0') return true;
        auto toLower = [](char ch) {
            return char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
        };
        const size_t fLen = std::strlen(filter);
        if (name.size() < fLen) return false;
        for (size_t i = 0; i + fLen <= name.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < fLen; ++j) {
                if (toLower(name[i + j]) != toLower(filter[j])) {
                    match = false; break;
                }
            }
            if (match) return true;
        }
        return false;
    }

    // 2D state-machine view. Returns true when a node moved or the
    // auto-fan-out kicked in, so the caller marks the controller
    // dirty + saves through to disk.
    bool DrawControllerGraph(SceneEditor::AnimatorControllerAsset& ctrl) {
        bool dirty = false;
        // Auto-fan-out: when every state still sits at (0, 0) treat
        // the controller as freshly authored and lay the nodes out in
        // a grid so the user doesn't have to drag them out manually.
        bool allZero = !ctrl.states.empty();
        for (const auto& s : ctrl.states) {
            if (s.nodeX != 0.0f || s.nodeY != 0.0f) {
                allZero = false;
                break;
            }
        }
        if (allZero) {
            for (int i = 0; i < int(ctrl.states.size()); ++i) {
                ctrl.states[size_t(i)].nodeX = 20.0f + (i % 4) * 150.0f;
                ctrl.states[size_t(i)].nodeY = 20.0f + (i / 4) *  70.0f;
            }
            dirty = true;
        }

        constexpr float kNodeW   = 130.0f;
        constexpr float kNodeH   =  40.0f;
        constexpr float kGraphH  = 280.0f;

        const ImVec2 panelMin = ImGui::GetCursorScreenPos();
        const ImVec2 size{ImGui::GetContentRegionAvail().x, kGraphH};
        const ImVec2 panelMax{panelMin.x + size.x, panelMin.y + size.y};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(panelMin, panelMax,
                          IM_COL32(28, 30, 36, 255));
        dl->AddRect(panelMin, panelMax,
                    IM_COL32(70, 70, 80, 255));
        ImGui::InvisibleButton("##graph_hit", size,
                               ImGuiButtonFlags_MouseButtonLeft);
        const bool hovered = ImGui::IsItemHovered();
        const ImGuiIO& io  = ImGui::GetIO();
        const ImVec2 mp    = io.MousePos;

        auto NodeRect = [&](const SceneEditor::AnimatorState& s,
                            ImVec2& a, ImVec2& b) {
            a = ImVec2{panelMin.x + s.nodeX,
                       panelMin.y + s.nodeY};
            b = ImVec2{a.x + kNodeW, a.y + kNodeH};
        };
        auto NodeCentre = [&](const SceneEditor::AnimatorState& s) {
            return ImVec2{panelMin.x + s.nodeX + kNodeW * 0.5f,
                          panelMin.y + s.nodeY + kNodeH * 0.5f};
        };

        // Distance from `p` to the line segment (a, b). Used by the
        // transition hit-test below; cached here so the rendering
        // loop can also flag the closest segment when the cursor
        // hovers over an arrow.
        auto SegDist = [](ImVec2 p, ImVec2 a, ImVec2 b) {
            const float dxv = b.x - a.x;
            const float dyv = b.y - a.y;
            const float len2 = dxv * dxv + dyv * dyv;
            if (len2 < 0.01f) {
                const float ex = p.x - a.x, ey = p.y - a.y;
                return std::sqrt(ex * ex + ey * ey);
            }
            float t = ((p.x - a.x) * dxv + (p.y - a.y) * dyv) / len2;
            t = std::clamp(t, 0.0f, 1.0f);
            const float px = a.x + t * dxv;
            const float py = a.y + t * dyv;
            const float ex = p.x - px, ey = p.y - py;
            return std::sqrt(ex * ex + ey * ey);
        };

        // Transitions first so node bodies overlay them.
        for (int ti = 0; ti < int(ctrl.transitions.size()); ++ti) {
            const auto& tr = ctrl.transitions[size_t(ti)];
            if (tr.fromIdx < 0 || tr.fromIdx >= int(ctrl.states.size())) continue;
            if (tr.toIdx   < 0 || tr.toIdx   >= int(ctrl.states.size())) continue;
            const ImVec2 srcC = NodeCentre(ctrl.states[size_t(tr.fromIdx)]);
            const ImVec2 dstC = NodeCentre(ctrl.states[size_t(tr.toIdx)]);
            const float dxa = dstC.x - srcC.x;
            const float dya = dstC.y - srcC.y;
            const float len = std::sqrt(dxa * dxa + dya * dya);
            if (len < 1.0f) continue;
            const float ux = dxa / len;
            const float uy = dya / len;
            const float pullBack = std::max(kNodeW, kNodeH) * 0.5f;
            const ImVec2 tip{dstC.x - ux * pullBack,
                             dstC.y - uy * pullBack};
            const bool selected = (ti == m_graphSelectedTransition);
            const ImU32 col = selected
                ? IM_COL32(255, 220, 60, 255)
                : IM_COL32(190, 190, 200, 220);
            dl->AddLine(srcC, tip, col, selected ? 3.0f : 1.5f);
            const float ah = selected ? 13.0f : 10.0f;
            const float aw = selected ?  6.0f :  5.0f;
            const ImVec2 head1{tip.x - ux * ah - uy * aw,
                               tip.y - uy * ah + ux * aw};
            const ImVec2 head2{tip.x - ux * ah + uy * aw,
                               tip.y - uy * ah - ux * aw};
            dl->AddTriangleFilled(tip, head1, head2, col);
        }

        auto HitNode = [&]() {
            for (int i = int(ctrl.states.size()) - 1; i >= 0; --i) {
                ImVec2 a, b;
                NodeRect(ctrl.states[size_t(i)], a, b);
                if (mp.x >= a.x && mp.x <= b.x &&
                    mp.y >= a.y && mp.y <= b.y) {
                    return i;
                }
            }
            return -1;
        };

        // Transition hit-test: closest valid edge within `kPick` px of
        // the cursor. Skips degenerate edges + the trailing "tip
        // pullback" segment naturally because we use the same
        // pullback the renderer does.
        auto HitTransition = [&]() {
            constexpr float kPick = 6.0f;
            int hit = -1;
            float best = kPick;
            for (int ti = 0; ti < int(ctrl.transitions.size()); ++ti) {
                const auto& tr = ctrl.transitions[size_t(ti)];
                if (tr.fromIdx < 0 || tr.fromIdx >= int(ctrl.states.size())) continue;
                if (tr.toIdx   < 0 || tr.toIdx   >= int(ctrl.states.size())) continue;
                const ImVec2 a = NodeCentre(ctrl.states[size_t(tr.fromIdx)]);
                const ImVec2 b = NodeCentre(ctrl.states[size_t(tr.toIdx)]);
                const float d = SegDist(mp, a, b);
                if (d < best) { best = d; hit = ti; }
            }
            return hit;
        };

        // Mouse-down node hit-test runs before the per-state body so
        // the drag state is set in time for the move logic below.
        // Transition selection takes priority over a deselect-state
        // click but yields to actual node hits.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const int hit = HitNode();
            if (m_graphPendingFrom >= 0) {
                if (hit >= 0 && hit != m_graphPendingFrom &&
                    m_graphPendingFrom < int(ctrl.states.size())) {
                    SceneEditor::AnimatorTransition tr;
                    tr.fromIdx   = m_graphPendingFrom;
                    tr.toIdx     = hit;
                    tr.valueBool = true;
                    ctrl.transitions.push_back(std::move(tr));
                    dirty = true;
                }
                m_graphPendingFrom = -1;
            } else if (hit >= 0) {
                m_graphSelectedState      = hit;
                m_graphDragState          = hit;
                m_graphSelectedTransition = -1;
            } else {
                const int trHit = HitTransition();
                if (trHit >= 0) {
                    m_graphSelectedTransition = trHit;
                    m_graphSelectedState      = -1;
                } else {
                    m_graphSelectedState      = -1;
                    m_graphSelectedTransition = -1;
                }
            }
        }
        // Right-click opens a node context menu.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            const int hit = HitNode();
            if (hit >= 0) {
                m_graphContextNode = hit;
                ImGui::OpenPopup("##graph_node_ctx");
            }
        }
        // Escape cancels the pending transition.
        if (m_graphPendingFrom >= 0 &&
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_graphPendingFrom = -1;
        }
        if (m_graphDragState >= 0 &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_graphDragState = -1;
        }
        if (m_graphDragState >= 0 &&
            m_graphDragState < int(ctrl.states.size())) {
            auto& s = ctrl.states[size_t(m_graphDragState)];
            if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
                s.nodeX = std::max(0.0f, s.nodeX + io.MouseDelta.x);
                s.nodeY = std::max(0.0f, s.nodeY + io.MouseDelta.y);
                s.nodeX = std::min(s.nodeX,
                                   std::max(0.0f, size.x - kNodeW));
                s.nodeY = std::min(s.nodeY,
                                   std::max(0.0f, size.y - kNodeH));
                dirty = true;
            }
        }

        // Draw nodes.
        for (int i = 0; i < int(ctrl.states.size()); ++i) {
            ImVec2 a, b;
            NodeRect(ctrl.states[size_t(i)], a, b);
            const bool isDefault = (i == ctrl.defaultState);
            const bool isSelected = (i == m_graphSelectedState);
            const ImU32 fill = isDefault
                ? IM_COL32(60, 100, 70, 255)
                : IM_COL32(60, 60, 80, 255);
            dl->AddRectFilled(a, b, fill, 4.0f);
            const ImU32 borderCol = isSelected
                ? IM_COL32(255, 220, 60, 255)
                : IM_COL32(180, 180, 180, 255);
            dl->AddRect(a, b, borderCol, 4.0f, 0,
                        isSelected ? 2.5f : 1.0f);
            // Centre-ish text. Truncate by clip rect; ImGui handles
            // the actual text wrap.
            dl->AddText(ImVec2{a.x + 8.0f, a.y + 12.0f},
                        IM_COL32(230, 230, 230, 255),
                        ctrl.states[size_t(i)].name.c_str());
            if (isDefault) {
                dl->AddText(ImVec2{a.x + 8.0f, a.y - 14.0f},
                            IM_COL32(150, 220, 170, 255),
                            "[default]");
            }
        }

        // Rubber-band + source highlight while waiting for the user
        // to click a target state.
        if (m_graphPendingFrom >= 0 &&
            m_graphPendingFrom < int(ctrl.states.size())) {
            const auto& src = ctrl.states[size_t(m_graphPendingFrom)];
            ImVec2 a, b;
            NodeRect(src, a, b);
            dl->AddRect(a, b, IM_COL32(255, 120, 120, 255),
                        4.0f, 0, 3.0f);
            const ImVec2 srcC = NodeCentre(src);
            dl->AddLine(srcC, mp,
                        IM_COL32(255, 220, 60, 220), 2.0f);
        }

        // Context menu. Lives outside the node iteration so the popup
        // can survive across frames + the user's mouse move.
        if (ImGui::BeginPopup("##graph_node_ctx")) {
            const int n = m_graphContextNode;
            if (ImGui::MenuItem("Add Transition From Here")) {
                m_graphPendingFrom = n;
            }
            if (ImGui::MenuItem("Make Default")) {
                if (n >= 0 && n < int(ctrl.states.size())) {
                    ctrl.defaultState = n;
                    dirty = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete State")) {
                if (n >= 0 && n < int(ctrl.states.size())) {
                    ctrl.states.erase(ctrl.states.begin() + n);
                    if (ctrl.defaultState == n) ctrl.defaultState = 0;
                    else if (ctrl.defaultState > n) ctrl.defaultState--;
                    ctrl.transitions.erase(
                        std::remove_if(ctrl.transitions.begin(),
                                       ctrl.transitions.end(),
                                       [&](const auto& t) {
                                           return t.fromIdx == n ||
                                                  t.toIdx   == n;
                                       }),
                        ctrl.transitions.end());
                    for (auto& t : ctrl.transitions) {
                        if (t.fromIdx > n) t.fromIdx--;
                        if (t.toIdx   > n) t.toIdx--;
                    }
                    if (m_graphSelectedState == n) m_graphSelectedState = -1;
                    if (m_graphDragState     == n) m_graphDragState     = -1;
                    if (m_graphPendingFrom   == n) m_graphPendingFrom   = -1;
                    dirty = true;
                }
            }
            ImGui::EndPopup();
        }

        // Legend.
        const char* legend = (m_graphPendingFrom >= 0)
            ? "Click a state to add a transition from the highlighted node. Esc cancels."
            : "Drag to reposition. Click an arrow to inspect, right-click a node for menus.";
        dl->AddText(ImVec2{panelMin.x + 8.0f, panelMax.y - 18.0f},
                    IM_COL32(140, 140, 150, 255), legend);

        // Inline inspector for the selected transition. Sits in the
        // parent layout right below the graph child region so it's
        // always visible alongside the highlighted arrow.
        if (m_graphSelectedTransition >= 0 &&
            m_graphSelectedTransition < int(ctrl.transitions.size())) {
            auto& tr = ctrl.transitions[size_t(m_graphSelectedTransition)];
            ImGui::PushID(50000 + m_graphSelectedTransition);
            ImGui::Text("Selected transition #%d:",
                        m_graphSelectedTransition);
            ImGui::SameLine();

            // From combo.
            const char* fromLabel = (tr.fromIdx < 0)
                ? "Any"
                : (tr.fromIdx < int(ctrl.states.size())
                    ? ctrl.states[size_t(tr.fromIdx)].name.c_str()
                    : "(invalid)");
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::BeginCombo("##from", fromLabel)) {
                if (ImGui::Selectable("Any", tr.fromIdx < 0)) {
                    tr.fromIdx = -1;
                    dirty = true;
                }
                for (int s = 0; s < int(ctrl.states.size()); ++s) {
                    const bool sel = (tr.fromIdx == s);
                    if (ImGui::Selectable(ctrl.states[size_t(s)].name.c_str(),
                                          sel)) {
                        tr.fromIdx = s;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("->");
            ImGui::SameLine();
            const char* toLabel =
                (tr.toIdx >= 0 && tr.toIdx < int(ctrl.states.size()))
                ? ctrl.states[size_t(tr.toIdx)].name.c_str()
                : "(invalid)";
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::BeginCombo("##to", toLabel)) {
                for (int s = 0; s < int(ctrl.states.size()); ++s) {
                    const bool sel = (tr.toIdx == s);
                    if (ImGui::Selectable(ctrl.states[size_t(s)].name.c_str(),
                                          sel)) {
                        tr.toIdx = s;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("when");
            ImGui::SameLine();
            const char* paramLabel = tr.param.empty() ? "(none)"
                                                     : tr.param.c_str();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::BeginCombo("##param", paramLabel)) {
                if (ImGui::Selectable("(none)", tr.param.empty())) {
                    tr.param.clear();
                    dirty = true;
                }
                for (const auto& p : ctrl.parameters) {
                    const bool sel = (p.name == tr.param);
                    if (ImGui::Selectable(p.name.c_str(), sel)) {
                        tr.param = p.name;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            // Op / value: same per-type rules the Transitions list
            // uses. Look up the parameter declaration so the right
            // ladder + widget shows up.
            const SceneEditor::AnimatorParameter* decl = nullptr;
            for (const auto& pr : ctrl.parameters) {
                if (pr.name == tr.param) { decl = &pr; break; }
            }
            using PT = SceneEditor::AnimatorParameter::Type;
            using OP = SceneEditor::AnimatorTransition::Op;
            const char* opNamesAll[] = { "==", "!=", "<", "<=", ">", ">=" };
            const bool numericParam =
                decl && (decl->type == PT::Float ||
                         decl->type == PT::Int);
            const int opCount = numericParam ? 6 : 2;
            int opIdx = int(tr.op);
            if (opIdx < 0 || opIdx >= opCount) opIdx = 0;
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::Combo("##gop", &opIdx, opNamesAll, opCount)) {
                tr.op = static_cast<OP>(opIdx);
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (decl && decl->type == PT::Float) {
                if (ImGui::DragFloat("##gval", &tr.valueFloat, 0.05f))
                    dirty = true;
            } else if (decl && decl->type == PT::Int) {
                if (ImGui::DragInt("##gval", &tr.valueInt)) dirty = true;
            } else if (decl && decl->type == PT::Trigger) {
                if (tr.valueBool != true) { tr.valueBool = true; dirty = true; }
                bool draft = true;
                ImGui::BeginDisabled();
                ImGui::Checkbox("##gval", &draft);
                ImGui::EndDisabled();
            } else if (decl && decl->type == PT::Vec2) {
                float v2[2] = { tr.valueVec2.x, tr.valueVec2.y };
                if (ImGui::DragFloat2("##gval", v2, 0.05f,
                                       0.0f, 0.0f, "%.2f")) {
                    tr.valueVec2 = { v2[0], v2[1] };
                    dirty = true;
                }
            } else if (decl && decl->type == PT::Color) {
                float col[4] = { tr.valueColor.r, tr.valueColor.g,
                                 tr.valueColor.b, tr.valueColor.a };
                if (ImGui::ColorEdit4("##gval", col,
                                       ImGuiColorEditFlags_NoLabel |
                                       ImGuiColorEditFlags_NoInputs |
                                       ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    tr.valueColor = { col[0], col[1], col[2], col[3] };
                    dirty = true;
                }
            } else if (decl && decl->type == PT::String) {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s",
                              tr.valueString.c_str());
                if (ImGui::InputText("##gval", buf, sizeof(buf))) {
                    tr.valueString = buf;
                    dirty = true;
                }
            } else {
                bool draft = tr.valueBool;
                if (ImGui::Checkbox("##gval", &draft)) {
                    tr.valueBool = draft;
                    dirty = true;
                }
            }
            ImGui::SameLine();
            bool exitDraft = tr.hasExitTime;
            if (ImGui::Checkbox("exit", &exitDraft)) {
                tr.hasExitTime = exitDraft;
                dirty = true;
            }
            if (tr.hasExitTime) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::SliderFloat("##gexitt", &tr.exitTime,
                                        0.0f, 1.0f, "%.2f")) dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("##gdur", &tr.duration, 0.01f,
                                  0.0f, 60.0f, "dur %.2fs")) {
                if (tr.duration < 0.0f) tr.duration = 0.0f;
                dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                ctrl.transitions.erase(ctrl.transitions.begin() +
                                       m_graphSelectedTransition);
                m_graphSelectedTransition = -1;
                dirty = true;
            }
            ImGui::PopID();
        }
        return dirty;
    }

    // AnimatorController Editor. Three sections: States, Parameters,
    // Transitions. Auto-saves to the .ncontroller file on every edit.
    void DrawControllerEditorWindow() {
        if (!m_controllerEditorOpen) return;
        if (m_controllerEditorActivate) {
            ImGui::SetNextWindowSize(ImVec2(820, 640), ImGuiCond_Appearing);
            ImGui::SetNextWindowFocus();
            m_controllerEditorActivate = false;
        }
        if (!ImGui::Begin("Animator Controller", &m_controllerEditorOpen)) {
            ImGui::End();
            return;
        }
        const SceneEditor::AssetEntry* entry =
            m_assetDb.FindByGuid(m_controllerEditorAsset);
        if (!entry || entry->typeName != "AnimatorController") {
            ImGui::TextDisabled("Double-click a .ncontroller in the "
                                "Project panel to edit it.");
            ImGui::End();
            return;
        }
        SceneEditor::AnimatorControllerAsset* ctrl =
            m_assetDb.GetAnimatorController(m_controllerEditorAsset);
        if (!ctrl) {
            ImGui::TextDisabled("Could not load '%s'.",
                                entry->relPath.c_str());
            ImGui::End();
            return;
        }
        ImGui::Text("%s", entry->relPath.c_str());
        bool dirty = false;

        // Default state combo. Falls back to "0 -- none" hint when
        // there are no states yet so the user knows what to do first.
        if (!ctrl->states.empty()) {
            const char* curName = (ctrl->defaultState >= 0 &&
                                   ctrl->defaultState < int(ctrl->states.size()))
                ? ctrl->states[size_t(ctrl->defaultState)].name.c_str()
                : "(invalid)";
            if (ImGui::BeginCombo("Default State", curName)) {
                for (int i = 0; i < int(ctrl->states.size()); ++i) {
                    const bool sel = (i == ctrl->defaultState);
                    if (ImGui::Selectable(ctrl->states[size_t(i)].name.c_str(),
                                          sel)) {
                        ctrl->defaultState = i;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("No states yet -- add one below.");
        }

        // Graph view sits between the Default State combo and the
        // detail-edit lists. Clicking a node selects it (and clears
        // the selection on empty-space click). Dragging a node
        // updates nodeX / nodeY in place and persists through the
        // shared `dirty` flag.
        if (DrawControllerGraph(*ctrl)) dirty = true;

        // -----------------------------------------------------------
        // States
        // -----------------------------------------------------------
        ImGui::Separator();
        ImGui::Text("States (%zu)", ctrl->states.size());
        int removeStateAt = -1;
        int moveStateUp   = -1;
        int moveStateDown = -1;
        int stateInsertAt = -1;
        for (int i = 0; i < int(ctrl->states.size()); ++i) {
            SceneEditor::AnimatorState& st = ctrl->states[size_t(i)];
            ImGui::PushID(i);
            if (ImGui::SmallButton("...##strow")) {
                ImGui::OpenPopup("strowctx");
            }
            if (ImGui::BeginPopup("strowctx")) {
                if (ImGui::MenuItem("Copy")) {
                    m_clipboardStates = { st };
                }
                ImGui::BeginDisabled(m_clipboardStates.empty());
                if (ImGui::MenuItem("Paste above")) stateInsertAt = i;
                if (ImGui::MenuItem("Paste below")) stateInsertAt = i + 1;
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::Text("%2d", i);
            ImGui::SameLine();
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", st.name.c_str());
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
                st.name = nameBuf;
                dirty = true;
            }
            ImGui::SameLine();
            // Animation combo.
            std::string animLabel = "(none)";
            if (st.animation.IsValid()) {
                if (const auto* e = m_assetDb.FindByGuid(st.animation)) {
                    animLabel = e->relPath;
                } else {
                    animLabel = st.animation.ToString();
                }
            }
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##anim", animLabel.c_str())) {
                if (ImGui::Selectable("(none)", !st.animation.IsValid())) {
                    st.animation = SceneEditor::AssetGuid{};
                    dirty = true;
                }
                for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                    if (a.typeName != "Animation") continue;
                    const bool sel = (a.guid == st.animation);
                    if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                        st.animation = a.guid;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            // Drop target on the animation combo: dragging a .nanim
            // from the Project panel assigns it without opening the
            // combo. Other asset types fall through (no assignment).
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p =
                        ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                    if (p->DataSize ==
                            sizeof(SceneEditor::AssetGuid)) {
                        SceneEditor::AssetGuid guid;
                        std::memcpy(&guid, p->Data, sizeof(guid));
                        if (const auto* a =
                                m_assetDb.FindByGuid(guid)) {
                            if (a->typeName == "Animation") {
                                st.animation = guid;
                                dirty = true;
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::DragFloat("##speed", &st.speed, 0.01f,
                                 -10.0f, 10.0f, "%.2fx")) {
                dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Up") && i > 0) moveStateUp = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") &&
                i + 1 < int(ctrl->states.size())) moveStateDown = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeStateAt = i;
            ImGui::PopID();
        }
        if (moveStateUp > 0) {
            std::swap(ctrl->states[size_t(moveStateUp)],
                      ctrl->states[size_t(moveStateUp - 1)]);
            dirty = true;
        }
        if (moveStateDown >= 0) {
            std::swap(ctrl->states[size_t(moveStateDown)],
                      ctrl->states[size_t(moveStateDown + 1)]);
            dirty = true;
        }
        if (removeStateAt >= 0) {
            ctrl->states.erase(ctrl->states.begin() + removeStateAt);
            // Reindex defaultState + transitions so nothing points at
            // a hole. Removing the defaultState falls back to 0; any
            // transition that referenced the removed index gets
            // dropped to keep the schema consistent.
            if (ctrl->defaultState == removeStateAt) ctrl->defaultState = 0;
            else if (ctrl->defaultState > removeStateAt) ctrl->defaultState--;
            ctrl->transitions.erase(
                std::remove_if(ctrl->transitions.begin(),
                               ctrl->transitions.end(),
                               [&](const auto& t) {
                                   return t.fromIdx == removeStateAt ||
                                          t.toIdx   == removeStateAt;
                               }),
                ctrl->transitions.end());
            for (auto& t : ctrl->transitions) {
                if (t.fromIdx > removeStateAt) t.fromIdx--;
                if (t.toIdx   > removeStateAt) t.toIdx--;
            }
            dirty = true;
        }
        if (stateInsertAt >= 0 && !m_clipboardStates.empty()) {
            // Inserting in the middle shifts every dependent index
            // that lands at or after the insertion point by the
            // paste count. Transitions + defaultState + graph
            // selection all need the bump in lockstep so the
            // controller stays internally consistent.
            const int count = int(m_clipboardStates.size());
            for (auto& t : ctrl->transitions) {
                if (t.fromIdx >= stateInsertAt) t.fromIdx += count;
                if (t.toIdx   >= stateInsertAt) t.toIdx   += count;
            }
            if (ctrl->defaultState >= stateInsertAt) {
                ctrl->defaultState += count;
            }
            auto bumpPtr = [&](int& v) {
                if (v >= stateInsertAt) v += count;
            };
            bumpPtr(m_graphSelectedState);
            bumpPtr(m_graphDragState);
            bumpPtr(m_graphPendingFrom);
            bumpPtr(m_graphContextNode);
            ctrl->states.insert(ctrl->states.begin() + stateInsertAt,
                                m_clipboardStates.begin(),
                                m_clipboardStates.end());
            dirty = true;
        }
        if (ImGui::Button("+ State")) {
            SceneEditor::AnimatorState st;
            st.name = "State" + std::to_string(ctrl->states.size());
            ctrl->states.push_back(std::move(st));
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sort A-Z") && ctrl->states.size() > 1) {
            // Sorting the states vector reorders the indices that
            // every transition + defaultState + graph selection
            // points at, so we build an old->new remap and rewrite
            // every dependent field in lockstep.
            const size_t N = ctrl->states.size();
            std::vector<size_t> order(N);
            for (size_t i = 0; i < N; ++i) order[i] = i;
            std::sort(order.begin(), order.end(),
                      [&](size_t a, size_t b) {
                          return ctrl->states[a].name <
                                 ctrl->states[b].name;
                      });
            std::vector<int> remap(N);
            for (size_t newIdx = 0; newIdx < N; ++newIdx) {
                remap[order[newIdx]] = int(newIdx);
            }
            std::vector<SceneEditor::AnimatorState> sorted;
            sorted.reserve(N);
            for (size_t i = 0; i < N; ++i) {
                sorted.push_back(std::move(ctrl->states[order[i]]));
            }
            ctrl->states = std::move(sorted);
            for (auto& tr : ctrl->transitions) {
                if (tr.fromIdx >= 0 && tr.fromIdx < int(N)) {
                    tr.fromIdx = remap[size_t(tr.fromIdx)];
                }
                if (tr.toIdx >= 0 && tr.toIdx < int(N)) {
                    tr.toIdx = remap[size_t(tr.toIdx)];
                }
            }
            if (ctrl->defaultState >= 0 &&
                ctrl->defaultState < int(N)) {
                ctrl->defaultState = remap[size_t(ctrl->defaultState)];
            }
            auto remapPtr = [&](int& v) {
                if (v >= 0 && v < int(N)) v = remap[size_t(v)];
            };
            remapPtr(m_graphSelectedState);
            remapPtr(m_graphDragState);
            remapPtr(m_graphPendingFrom);
            remapPtr(m_graphContextNode);
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Auto layout") && !ctrl->states.empty()) {
            // Lay nodes out on an approximately square grid so the
            // graph view starts from a readable baseline. The
            // graph editor's drag handles still move individual
            // nodes after.
            const int N    = int(ctrl->states.size());
            int cols = 1;
            while (cols * cols < N) ++cols;
            const float spacingX = 200.0f;
            const float spacingY = 120.0f;
            const float originX  = 40.0f;
            const float originY  = 40.0f;
            for (int i = 0; i < N; ++i) {
                const int row = i / cols;
                const int col = i % cols;
                ctrl->states[size_t(i)].nodeX = originX + col * spacingX;
                ctrl->states[size_t(i)].nodeY = originY + row * spacingY;
            }
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy##states") && !ctrl->states.empty()) {
            m_clipboardStates = ctrl->states;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copy %zu states to clipboard.",
                              ctrl->states.size());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_clipboardStates.empty());
        if (ImGui::Button("Paste##states")) {
            // Appending states never invalidates existing
            // transitions because their fromIdx + toIdx still
            // point at the original slots.
            for (const auto& s : m_clipboardStates) {
                ctrl->states.push_back(s);
            }
            dirty = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                m_clipboardStates.empty()
                    ? "Clipboard empty."
                    : "Append %zu states from clipboard.",
                m_clipboardStates.size());
        }

        // -----------------------------------------------------------
        // Parameters
        // -----------------------------------------------------------
        ImGui::Separator();
        ImGui::Text("Parameters (%zu)", ctrl->parameters.size());
        {
            const char* typeFilterNames[] = {
                "All", "Bool", "Float", "Int", "Trigger",
                "Vec2", "Color", "String" };
            ImGui::SetNextItemWidth(90.0f);
            ImGui::Combo("##ctrlparamtype",
                         &m_controllerEditorParamTypeFilter,
                         typeFilterNames, 8);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##ctrlparamfilter", "Filter parameters...",
                m_controllerEditorParamFilter,
                sizeof(m_controllerEditorParamFilter));
        }
        int removeParamAt = -1;
        int paramInsertAt = -1;
        for (int i = 0; i < int(ctrl->parameters.size()); ++i) {
            SceneEditor::AnimatorParameter& p = ctrl->parameters[size_t(i)];
            if (!ParamTypeMatchesFilter(
                    p.type, m_controllerEditorParamTypeFilter)) {
                continue;
            }
            if (!ParamNameMatchesFilter(
                    p.name, m_controllerEditorParamFilter)) {
                continue;
            }
            ImGui::PushID(10000 + i);
            if (ImGui::SmallButton("...##prow")) {
                ImGui::OpenPopup("prowctx");
            }
            if (ImGui::BeginPopup("prowctx")) {
                if (ImGui::MenuItem("Copy")) {
                    m_clipboardParameters = { p };
                }
                ImGui::BeginDisabled(m_clipboardParameters.empty());
                if (ImGui::MenuItem("Paste above")) paramInsertAt = i;
                if (ImGui::MenuItem("Paste below")) paramInsertAt = i + 1;
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", p.name.c_str());
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputText("##pname", nameBuf, sizeof(nameBuf))) {
                p.name = nameBuf;
                dirty = true;
            }
            ImGui::SameLine();
            const char* typeNames[] = { "Bool", "Float", "Int", "Trigger",
                                        "Vec2", "Color", "String" };
            int typeIdx = int(p.type);
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::Combo("##ptype", &typeIdx, typeNames, 7)) {
                p.type = static_cast<SceneEditor::AnimatorParameter::Type>(typeIdx);
                dirty = true;
            }
            ImGui::SameLine();
            using PT = SceneEditor::AnimatorParameter::Type;
            if (p.type == PT::Float) {
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::DragFloat("##pdef", &p.defaultFloat, 0.05f,
                                     0.0f, 0.0f, "def %.2f")) dirty = true;
            } else if (p.type == PT::Int) {
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::DragInt("##pdef", &p.defaultInt, 0.2f,
                                    0, 0, "def %d")) dirty = true;
            } else if (p.type == PT::Trigger) {
                ImGui::TextDisabled("(auto-reset)");
            } else if (p.type == PT::Vec2) {
                float v2[2] = { p.defaultVec2.x, p.defaultVec2.y };
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::DragFloat2("##pdef", v2, 0.05f,
                                       0.0f, 0.0f, "%.2f")) {
                    p.defaultVec2 = { v2[0], v2[1] };
                    dirty = true;
                }
            } else if (p.type == PT::Color) {
                float col[4] = { p.defaultColor.r, p.defaultColor.g,
                                 p.defaultColor.b, p.defaultColor.a };
                if (ImGui::ColorEdit4("##pdef", col,
                                       ImGuiColorEditFlags_NoLabel |
                                       ImGuiColorEditFlags_NoInputs |
                                       ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    p.defaultColor = { col[0], col[1], col[2], col[3] };
                    dirty = true;
                }
            } else if (p.type == PT::String) {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s",
                              p.defaultString.c_str());
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::InputTextWithHint("##pdef", "default",
                                              buf, sizeof(buf))) {
                    p.defaultString = buf;
                    dirty = true;
                }
            } else {
                bool draft = p.defaultBool;
                if (ImGui::Checkbox("default", &draft)) {
                    p.defaultBool = draft;
                    dirty = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeParamAt = i;
            ImGui::PopID();
        }
        if (removeParamAt >= 0) {
            ctrl->parameters.erase(ctrl->parameters.begin() + removeParamAt);
            dirty = true;
        }
        if (paramInsertAt >= 0 && !m_clipboardParameters.empty()) {
            ctrl->parameters.insert(
                ctrl->parameters.begin() + paramInsertAt,
                m_clipboardParameters.begin(),
                m_clipboardParameters.end());
            dirty = true;
        }
        if (ImGui::Button("+ Param")) {
            SceneEditor::AnimatorParameter p;
            p.name = "param" + std::to_string(ctrl->parameters.size());
            ctrl->parameters.push_back(std::move(p));
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sort A-Z") && ctrl->parameters.size() > 1) {
            std::sort(ctrl->parameters.begin(), ctrl->parameters.end(),
                      [](const SceneEditor::AnimatorParameter& a,
                         const SceneEditor::AnimatorParameter& b) {
                          return a.name < b.name;
                      });
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sort by type") && ctrl->parameters.size() > 1) {
            // Stable enough: tie-break by name so Bool params sort
            // alphabetically inside their group, then Float, etc.
            std::sort(ctrl->parameters.begin(), ctrl->parameters.end(),
                      [](const SceneEditor::AnimatorParameter& a,
                         const SceneEditor::AnimatorParameter& b) {
                          if (a.type != b.type) {
                              return int(a.type) < int(b.type);
                          }
                          return a.name < b.name;
                      });
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy##params") && !ctrl->parameters.empty()) {
            m_clipboardParameters = ctrl->parameters;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copy %zu parameters to clipboard.",
                              ctrl->parameters.size());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_clipboardParameters.empty());
        if (ImGui::Button("Paste##params")) {
            for (const auto& p : m_clipboardParameters) {
                ctrl->parameters.push_back(p);
            }
            dirty = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                m_clipboardParameters.empty()
                    ? "Clipboard empty."
                    : "Append %zu parameters from clipboard.",
                m_clipboardParameters.size());
        }

        // -----------------------------------------------------------
        // Transitions
        // -----------------------------------------------------------
        ImGui::Separator();
        ImGui::Text("Transitions (%zu)", ctrl->transitions.size());
        // Filter row: 3 BeginCombos -- From / To / Param. Each filter
        // takes 0 = "Show all" and otherwise maps to either a state
        // index, the global (fromIdx == -1) bucket, or the empty-
        // param bucket.
        {
            const auto& states = ctrl->states;
            const auto& params = ctrl->parameters;
            // Clamp filters that went stale after the user removed
            // a state / parameter while the combo still pointed at
            // its slot.
            if (m_controllerEditorTrFromFilter >
                int(states.size()) + 1) {
                m_controllerEditorTrFromFilter = 0;
            }
            if (m_controllerEditorTrToFilter > int(states.size())) {
                m_controllerEditorTrToFilter = 0;
            }
            if (m_controllerEditorTrParamFilter >
                int(params.size()) + 1) {
                m_controllerEditorTrParamFilter = 0;
            }
            auto fromLabelFor = [&](int idx) -> const char* {
                if (idx == 0) return "From: all";
                if (idx == 1) return "From: (global)";
                const int s = idx - 2;
                return (s >= 0 && s < int(states.size()))
                    ? states[size_t(s)].name.c_str() : "(invalid)";
            };
            auto toLabelFor = [&](int idx) -> const char* {
                if (idx == 0) return "To: all";
                const int s = idx - 1;
                return (s >= 0 && s < int(states.size()))
                    ? states[size_t(s)].name.c_str() : "(invalid)";
            };
            auto paramLabelFor = [&](int idx) -> const char* {
                if (idx == 0) return "Param: all";
                if (idx == 1) return "Param: (none)";
                const int p = idx - 2;
                return (p >= 0 && p < int(params.size()))
                    ? params[size_t(p)].name.c_str() : "(invalid)";
            };
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::BeginCombo("##trfromfilter",
                    fromLabelFor(m_controllerEditorTrFromFilter))) {
                if (ImGui::Selectable("From: all",
                        m_controllerEditorTrFromFilter == 0)) {
                    m_controllerEditorTrFromFilter = 0;
                }
                if (ImGui::Selectable("From: (global)",
                        m_controllerEditorTrFromFilter == 1)) {
                    m_controllerEditorTrFromFilter = 1;
                }
                for (int s = 0; s < int(states.size()); ++s) {
                    const int entry = s + 2;
                    if (ImGui::Selectable(states[size_t(s)].name.c_str(),
                            m_controllerEditorTrFromFilter == entry)) {
                        m_controllerEditorTrFromFilter = entry;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::BeginCombo("##trtofilter",
                    toLabelFor(m_controllerEditorTrToFilter))) {
                if (ImGui::Selectable("To: all",
                        m_controllerEditorTrToFilter == 0)) {
                    m_controllerEditorTrToFilter = 0;
                }
                for (int s = 0; s < int(states.size()); ++s) {
                    const int entry = s + 1;
                    if (ImGui::Selectable(states[size_t(s)].name.c_str(),
                            m_controllerEditorTrToFilter == entry)) {
                        m_controllerEditorTrToFilter = entry;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::BeginCombo("##trparamfilter",
                    paramLabelFor(m_controllerEditorTrParamFilter))) {
                if (ImGui::Selectable("Param: all",
                        m_controllerEditorTrParamFilter == 0)) {
                    m_controllerEditorTrParamFilter = 0;
                }
                if (ImGui::Selectable("Param: (none)",
                        m_controllerEditorTrParamFilter == 1)) {
                    m_controllerEditorTrParamFilter = 1;
                }
                for (int p = 0; p < int(params.size()); ++p) {
                    const int entry = p + 2;
                    if (ImGui::Selectable(params[size_t(p)].name.c_str(),
                            m_controllerEditorTrParamFilter == entry)) {
                        m_controllerEditorTrParamFilter = entry;
                    }
                }
                ImGui::EndCombo();
            }
        }
        // True when the transition survives every active filter
        // (AND across all three axes). Combo entry semantics match
        // the storage layout described on the field declaration.
        auto transitionPassesFilter = [&](
            const SceneEditor::AnimatorTransition& tr) {
            if (m_controllerEditorTrFromFilter > 0) {
                const int want = m_controllerEditorTrFromFilter - 2;
                if (m_controllerEditorTrFromFilter == 1) {
                    if (tr.fromIdx != -1) return false;
                } else if (tr.fromIdx != want) {
                    return false;
                }
            }
            if (m_controllerEditorTrToFilter > 0) {
                const int want = m_controllerEditorTrToFilter - 1;
                if (tr.toIdx != want) return false;
            }
            if (m_controllerEditorTrParamFilter > 0) {
                if (m_controllerEditorTrParamFilter == 1) {
                    if (!tr.param.empty()) return false;
                } else {
                    const int idx = m_controllerEditorTrParamFilter - 2;
                    if (idx < 0 ||
                        idx >= int(ctrl->parameters.size())) return false;
                    if (tr.param !=
                        ctrl->parameters[size_t(idx)].name) return false;
                }
            }
            return true;
        };
        int removeTrAt = -1;
        int moveTrUp   = -1;
        int moveTrDown = -1;
        int trInsertAt = -1;
        for (int i = 0; i < int(ctrl->transitions.size()); ++i) {
            SceneEditor::AnimatorTransition& tr = ctrl->transitions[size_t(i)];
            if (!transitionPassesFilter(tr)) continue;
            ImGui::PushID(20000 + i);
            if (ImGui::SmallButton("...##trrow")) {
                ImGui::OpenPopup("trrowctx");
            }
            if (ImGui::BeginPopup("trrowctx")) {
                if (ImGui::MenuItem("Copy")) {
                    m_clipboardTransitions = { tr };
                }
                ImGui::BeginDisabled(m_clipboardTransitions.empty());
                if (ImGui::MenuItem("Paste above")) trInsertAt = i;
                if (ImGui::MenuItem("Paste below")) trInsertAt = i + 1;
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            // From combo: "Any" (-1) + state names.
            const char* fromLabel = (tr.fromIdx < 0)
                ? "Any"
                : (tr.fromIdx < int(ctrl->states.size())
                    ? ctrl->states[size_t(tr.fromIdx)].name.c_str()
                    : "(invalid)");
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::BeginCombo("##from", fromLabel)) {
                if (ImGui::Selectable("Any", tr.fromIdx < 0)) {
                    tr.fromIdx = -1;
                    dirty = true;
                }
                for (int s = 0; s < int(ctrl->states.size()); ++s) {
                    const bool sel = (tr.fromIdx == s);
                    if (ImGui::Selectable(ctrl->states[size_t(s)].name.c_str(),
                                          sel)) {
                        tr.fromIdx = s;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("->");
            ImGui::SameLine();
            const char* toLabel =
                (tr.toIdx >= 0 && tr.toIdx < int(ctrl->states.size()))
                ? ctrl->states[size_t(tr.toIdx)].name.c_str()
                : "(invalid)";
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::BeginCombo("##to", toLabel)) {
                for (int s = 0; s < int(ctrl->states.size()); ++s) {
                    const bool sel = (tr.toIdx == s);
                    if (ImGui::Selectable(ctrl->states[size_t(s)].name.c_str(),
                                          sel)) {
                        tr.toIdx = s;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("when");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            const char* paramLabel = tr.param.empty() ? "(none)"
                                                     : tr.param.c_str();
            if (ImGui::BeginCombo("##param", paramLabel)) {
                if (ImGui::Selectable("(none)", tr.param.empty())) {
                    tr.param.clear();
                    dirty = true;
                }
                for (const auto& p : ctrl->parameters) {
                    const bool sel = (p.name == tr.param);
                    if (ImGui::Selectable(p.name.c_str(), sel)) {
                        tr.param = p.name;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            // Op combo. Float / Int expose the full ladder; Bool /
            // Trigger only get == / != (other comparisons are noise
            // for bool semantics). Trigger transitions always read
            // valueBool == true in practice -- we render the
            // checkbox anyway so the user can mirror an explicit
            // false-check if they want.
            const SceneEditor::AnimatorParameter* decl = nullptr;
            for (const auto& pr : ctrl->parameters) {
                if (pr.name == tr.param) { decl = &pr; break; }
            }
            using PT = SceneEditor::AnimatorParameter::Type;
            using OP = SceneEditor::AnimatorTransition::Op;
            const char* opNamesAll[] = { "==", "!=", "<", "<=", ">", ">=" };
            const bool numericParam =
                decl && (decl->type == PT::Float ||
                         decl->type == PT::Int);
            const int opCount = numericParam ? 6 : 2;
            int opIdx = int(tr.op);
            if (opIdx < 0 || opIdx >= opCount) opIdx = 0;
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::Combo("##op", &opIdx, opNamesAll, opCount)) {
                tr.op = static_cast<OP>(opIdx);
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (decl && decl->type == PT::Float) {
                if (ImGui::DragFloat("##val", &tr.valueFloat, 0.05f)) dirty = true;
            } else if (decl && decl->type == PT::Int) {
                if (ImGui::DragInt("##val", &tr.valueInt)) dirty = true;
            } else if (decl && decl->type == PT::Trigger) {
                if (tr.valueBool != true) { tr.valueBool = true; dirty = true; }
                bool draft = true;
                ImGui::BeginDisabled();
                ImGui::Checkbox("##val", &draft);
                ImGui::EndDisabled();
            } else if (decl && decl->type == PT::Vec2) {
                float v2[2] = { tr.valueVec2.x, tr.valueVec2.y };
                if (ImGui::DragFloat2("##val", v2, 0.05f,
                                       0.0f, 0.0f, "%.2f")) {
                    tr.valueVec2 = { v2[0], v2[1] };
                    dirty = true;
                }
            } else if (decl && decl->type == PT::Color) {
                float col[4] = { tr.valueColor.r, tr.valueColor.g,
                                 tr.valueColor.b, tr.valueColor.a };
                if (ImGui::ColorEdit4("##val", col,
                                       ImGuiColorEditFlags_NoLabel |
                                       ImGuiColorEditFlags_NoInputs |
                                       ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    tr.valueColor = { col[0], col[1], col[2], col[3] };
                    dirty = true;
                }
            } else if (decl && decl->type == PT::String) {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s",
                              tr.valueString.c_str());
                if (ImGui::InputText("##val", buf, sizeof(buf))) {
                    tr.valueString = buf;
                    dirty = true;
                }
            } else {
                bool draft = tr.valueBool;
                if (ImGui::Checkbox("##val", &draft)) {
                    tr.valueBool = draft;
                    dirty = true;
                }
            }
            ImGui::SameLine();
            // Exit-time gate: when checked, the slider sets the
            // normalised threshold (0..1) the source animation has to
            // play past before the transition is allowed to fire.
            bool exitDraft = tr.hasExitTime;
            if (ImGui::Checkbox("exit", &exitDraft)) {
                tr.hasExitTime = exitDraft;
                dirty = true;
            }
            if (tr.hasExitTime) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::SliderFloat("##exitt", &tr.exitTime,
                                        0.0f, 1.0f, "%.2f")) dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("##dur", &tr.duration, 0.01f,
                                  0.0f, 60.0f, "dur %.2fs")) {
                if (tr.duration < 0.0f) tr.duration = 0.0f;
                dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Up") && i > 0) moveTrUp = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") &&
                i + 1 < int(ctrl->transitions.size())) moveTrDown = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeTrAt = i;
            ImGui::PopID();
        }
        if (moveTrUp > 0) {
            std::swap(ctrl->transitions[size_t(moveTrUp)],
                      ctrl->transitions[size_t(moveTrUp - 1)]);
            dirty = true;
        }
        if (moveTrDown >= 0) {
            std::swap(ctrl->transitions[size_t(moveTrDown)],
                      ctrl->transitions[size_t(moveTrDown + 1)]);
            dirty = true;
        }
        if (removeTrAt >= 0) {
            ctrl->transitions.erase(ctrl->transitions.begin() + removeTrAt);
            dirty = true;
        }
        if (trInsertAt >= 0 && !m_clipboardTransitions.empty()) {
            // Per-row paste reuses the M5c33 clamp: edges whose
            // fromIdx / toIdx fall outside the current state count
            // land at safe sentinels so the row stays editable
            // instead of crashing the runtime.
            const int N = int(ctrl->states.size());
            std::vector<SceneEditor::AnimatorTransition> pending;
            pending.reserve(m_clipboardTransitions.size());
            for (const auto& src : m_clipboardTransitions) {
                SceneEditor::AnimatorTransition copy = src;
                if (copy.fromIdx >= N) copy.fromIdx = -1;
                if (copy.toIdx < 0 || copy.toIdx >= N) {
                    copy.toIdx = (N > 0) ? 0 : -1;
                }
                pending.push_back(std::move(copy));
            }
            ctrl->transitions.insert(
                ctrl->transitions.begin() + trInsertAt,
                pending.begin(), pending.end());
            dirty = true;
        }
        if (ImGui::Button("+ Transition") && !ctrl->states.empty()) {
            SceneEditor::AnimatorTransition t;
            t.fromIdx   = -1;
            t.toIdx     = 0;
            t.valueBool = true;
            ctrl->transitions.push_back(std::move(t));
            dirty = true;
        }
        // Selection snapshot helper -- both sort buttons want to
        // anchor m_graphSelectedTransition to the same edge after
        // the reorder.
        auto SortTransitions = [&](auto&& cmp) {
            SceneEditor::AnimatorTransition selSnap;
            bool hadSel = false;
            if (m_graphSelectedTransition >= 0 &&
                m_graphSelectedTransition <
                    int(ctrl->transitions.size())) {
                selSnap = ctrl->transitions[
                    size_t(m_graphSelectedTransition)];
                hadSel = true;
            }
            std::sort(ctrl->transitions.begin(),
                      ctrl->transitions.end(), cmp);
            if (hadSel) {
                m_graphSelectedTransition = -1;
                for (size_t i = 0; i < ctrl->transitions.size(); ++i) {
                    const auto& t = ctrl->transitions[i];
                    if (t.fromIdx == selSnap.fromIdx &&
                        t.toIdx   == selSnap.toIdx   &&
                        t.param   == selSnap.param) {
                        m_graphSelectedTransition = int(i);
                        break;
                    }
                }
            }
            dirty = true;
        };
        ImGui::SameLine();
        if (ImGui::Button("Sort by source") &&
            ctrl->transitions.size() > 1) {
            // Group by fromIdx, then toIdx, then param so edges
            // emanating from the same state cluster together and
            // the order inside each cluster stays predictable.
            SortTransitions([](const SceneEditor::AnimatorTransition& a,
                               const SceneEditor::AnimatorTransition& b) {
                if (a.fromIdx != b.fromIdx) return a.fromIdx < b.fromIdx;
                if (a.toIdx   != b.toIdx)   return a.toIdx   < b.toIdx;
                return a.param < b.param;
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Sort by param") &&
            ctrl->transitions.size() > 1) {
            // Group by param (so every edge reading the same
            // parameter sits together), then fromIdx + toIdx.
            SortTransitions([](const SceneEditor::AnimatorTransition& a,
                               const SceneEditor::AnimatorTransition& b) {
                if (a.param   != b.param)   return a.param   < b.param;
                if (a.fromIdx != b.fromIdx) return a.fromIdx < b.fromIdx;
                return a.toIdx < b.toIdx;
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy##trans") &&
            !ctrl->transitions.empty()) {
            m_clipboardTransitions = ctrl->transitions;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copy %zu transitions to clipboard.",
                              ctrl->transitions.size());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_clipboardTransitions.empty());
        if (ImGui::Button("Paste##trans")) {
            // Transitions reference state indices, so the paste
            // path clamps each pasted edge against the target's
            // current state count: out-of-range fromIdx drops to
            // -1 (any), out-of-range toIdx drops to 0 (or stays
            // -1 if the target has no states at all -- still a
            // visible "(invalid)" the user will fix in the row
            // editor). param strings stay as-is; a stale param
            // name just means the transition won't fire until the
            // user updates it or adds a matching parameter.
            const int N = int(ctrl->states.size());
            for (const auto& tr : m_clipboardTransitions) {
                SceneEditor::AnimatorTransition copy = tr;
                if (copy.fromIdx >= N) copy.fromIdx = -1;
                if (copy.toIdx < 0 || copy.toIdx >= N) {
                    copy.toIdx = (N > 0) ? 0 : -1;
                }
                ctrl->transitions.push_back(std::move(copy));
            }
            dirty = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                m_clipboardTransitions.empty()
                    ? "Clipboard empty."
                    : "Append %zu transitions from clipboard "
                      "(state indices clamp to the target controller).",
                m_clipboardTransitions.size());
        }

        if (dirty) m_assetDb.SaveAnimatorController(m_controllerEditorAsset);
        ImGui::End();
    }

    // Loads `absPath` into m_scriptEditorBuffer + makes the
    // editor window visible. relPath is the project-relative
    // path the title bar shows so absolute paths don't bloat
    // the window header. Truncated reads (file > 64 KB) flip
    // m_scriptEditorTruncated; Save remains disabled while
    // that flag is set so a partial buffer can't overwrite a
    // larger source file.
    void OpenScriptEditor(const std::string& absPath,
                          const std::string& relPath) {
        m_scriptEditorBuffer.assign(kScriptEditorBufSize, '\0');
        m_scriptEditorTruncated = false;
        FILE* f = nullptr;
        fopen_s(&f, absPath.c_str(), "rb");
        if (f) {
            const size_t n = std::fread(m_scriptEditorBuffer.data(),
                                         1, kScriptEditorBufSize - 1, f);
            // Mark truncated if there's more after the read.
            const int peek = std::fgetc(f);
            if (peek != EOF) m_scriptEditorTruncated = true;
            std::fclose(f);
            m_scriptEditorBuffer[n] = '\0';
        } else {
            m_scriptEditorBuffer[0] = '\0';
        }
        m_scriptEditorAbsPath  = absPath;
        m_scriptEditorRelPath  = relPath;
        m_scriptEditorOpen     = true;
        m_scriptEditorActivate = true;
    }

    // Writes the editor buffer back to disk + re-runs the
    // script through ScriptHost::LoadFile so the interpreter
    // picks up the change without a project reload. No-op when
    // the editor isn't open or the buffer is flagged truncated.
    void SaveScriptEditor() {
        if (!m_scriptEditorOpen) return;
        if (m_scriptEditorTruncated) return;
        FILE* f = nullptr;
        fopen_s(&f, m_scriptEditorAbsPath.c_str(), "wb");
        if (!f) return;
        const size_t len = std::strlen(m_scriptEditorBuffer.data());
        std::fwrite(m_scriptEditorBuffer.data(), 1, len, f);
        std::fclose(f);
        m_scriptHost.LoadFile(m_scriptEditorAbsPath,
                              m_scriptEditorRelPath);
    }

    // Discards in-memory edits + re-reads from disk. Same code
    // path as the initial open; just rewires absPath / relPath
    // back to what's already set.
    void ReloadScriptEditor() {
        if (!m_scriptEditorOpen) return;
        OpenScriptEditor(m_scriptEditorAbsPath,
                         m_scriptEditorRelPath);
    }

    // Floating panel that hosts a single InputTextMultiline +
    // Save / Reload / Close buttons. Optional truncation banner
    // when the source file exceeded the 64 KB buffer.
    void DrawScriptEditorWindow() {
        if (!m_scriptEditorOpen) return;
        if (m_scriptEditorActivate) {
            ImGui::SetNextWindowFocus();
            m_scriptEditorActivate = false;
        }
        ImGui::SetNextWindowSize(ImVec2(720, 520),
                                  ImGuiCond_FirstUseEver);
        std::string title = "Script: " + m_scriptEditorRelPath +
                            "###scripteditor";
        if (!ImGui::Begin(title.c_str(), &m_scriptEditorOpen)) {
            ImGui::End();
            return;
        }
        if (ImGui::Button("Save")) SaveScriptEditor();
        ImGui::SameLine();
        if (ImGui::Button("Reload from disk")) ReloadScriptEditor();
        ImGui::SameLine();
        if (ImGui::Button("Close")) m_scriptEditorOpen = false;
        if (m_scriptEditorTruncated) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                "(truncated -- file > 64 KB, Save disabled)");
        }
        ImGui::Separator();
        // Fill the remaining region with the text widget. The
        // buffer's NUL keeps trailing capacity invisible to the
        // widget while still letting the user grow the file.
        ImGui::InputTextMultiline("##scripttext",
            m_scriptEditorBuffer.data(),
            m_scriptEditorBuffer.size(),
            ImVec2(-FLT_MIN, -FLT_MIN),
            ImGuiInputTextFlags_AllowTabInput);
        ImGui::End();
    }

    void OpenAnimationEditor(const SceneEditor::AssetGuid& guid) {
        m_animEditorAsset      = guid;
        m_animEditorOpen       = true;
        m_animEditorActivate   = true;
        m_animEditorScrubTime  = 0.0f;
        m_animEditorSelEvent   = -1;
        m_animEditorEventFilter[0] = '\0';
        m_animEditorAutoSort   = false;
        m_animEditorFrameFilter[0] = '\0';
        m_animEditorApplyDuration = 0.1f;
    }

    // Animation Editor window. Top: texture + loop. Middle: frame
    // list with per-row sub-sprite Combo + duration + reorder /
    // remove. Bottom: scrubber that samples the current frame and
    // outlines that frame's source rect on the texture preview.
    void DrawAnimationEditorWindow() {
        if (!m_animEditorOpen) return;
        if (m_animEditorActivate) {
            ImGui::SetNextWindowSize(ImVec2(820, 620), ImGuiCond_Appearing);
            ImGui::SetNextWindowFocus();
            m_animEditorActivate = false;
        }
        if (!ImGui::Begin("Animation Editor", &m_animEditorOpen)) {
            ImGui::End();
            return;
        }
        const SceneEditor::AssetEntry* entry =
            m_assetDb.FindByGuid(m_animEditorAsset);
        if (!entry || entry->typeName != "Animation") {
            ImGui::TextDisabled("Double-click a .nanim in the Project "
                                "panel to edit it.");
            ImGui::End();
            return;
        }
        SceneEditor::AnimationAsset* an =
            m_assetDb.GetAnimation(m_animEditorAsset);
        if (!an) {
            ImGui::TextDisabled("Could not load '%s'.",
                                entry->relPath.c_str());
            ImGui::End();
            return;
        }
        ImGui::Text("%s", entry->relPath.c_str());
        bool dirty = false;

        // Texture combo.
        std::string current = "(none)";
        if (an->texture.IsValid()) {
            if (const auto* te = m_assetDb.FindByGuid(an->texture)) {
                current = te->relPath;
            } else {
                current = an->texture.ToString();
            }
        }
        if (ImGui::BeginCombo("Texture", current.c_str())) {
            if (ImGui::Selectable("(none)", !an->texture.IsValid())) {
                an->texture = SceneEditor::AssetGuid{};
                dirty = true;
            }
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Texture") continue;
                const bool sel = (a.guid == an->texture);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    an->texture = a.guid;
                    dirty = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox("Loop", &an->loop)) dirty = true;

        // Pre-resolve the texture's sub-sprite name list so the
        // per-frame Combo can offer it without re-parsing every row.
        std::vector<std::string> subNames;
        if (const auto* te = m_assetDb.FindByGuid(an->texture)) {
            SceneEditor::TextureImportSettings imp;
            imp.FromJson(te->importSettings);
            for (const auto& s : imp.spriteSheet.sprites) {
                subNames.push_back(s.name);
            }
        }
        if (subNames.empty()) {
            ImGui::TextDisabled("Texture has no Multiple-mode sprite "
                                "sheet -- open the Sprite Editor on "
                                "the texture to slice it first.");
        }

        ImGui::Separator();
        // Frame list.
        ImGui::Text("Frames (%zu)", an->frames.size());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##framefilter", "Filter frames by sprite...",
            m_animEditorFrameFilter, sizeof(m_animEditorFrameFilter));
        int  removeAt = -1;
        int  moveUp   = -1;
        int  moveDown = -1;
        int  frameInsertAt = -1;
        for (size_t i = 0; i < an->frames.size(); ++i) {
            SceneEditor::AnimationFrame& fr = an->frames[i];
            if (!ParamNameMatchesFilter(
                    fr.sprite, m_animEditorFrameFilter)) {
                continue;
            }
            ImGui::PushID(int(i));
            if (ImGui::SmallButton("...##frrow")) {
                ImGui::OpenPopup("frrowctx");
            }
            if (ImGui::BeginPopup("frrowctx")) {
                if (ImGui::MenuItem("Copy")) {
                    m_clipboardFrames = { fr };
                }
                ImGui::BeginDisabled(m_clipboardFrames.empty());
                if (ImGui::MenuItem("Paste above")) {
                    frameInsertAt = int(i);
                }
                if (ImGui::MenuItem("Paste below")) {
                    frameInsertAt = int(i) + 1;
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::Text("%2zu", i);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            const char* preview = fr.sprite.empty() ? "(none)"
                                                    : fr.sprite.c_str();
            if (ImGui::BeginCombo("##sprite", preview)) {
                for (const std::string& n : subNames) {
                    const bool sel = (n == fr.sprite);
                    if (ImGui::Selectable(n.c_str(), sel)) {
                        fr.sprite = n;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat("##dur", &fr.duration, 0.01f,
                                 0.01f, 60.0f, "%.3fs")) {
                dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Up") && i > 0)
                moveUp = int(i);
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") && i + 1 < an->frames.size())
                moveDown = int(i);
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeAt = int(i);
            ImGui::PopID();
        }
        if (moveUp > 0) {
            std::swap(an->frames[size_t(moveUp)],
                      an->frames[size_t(moveUp - 1)]);
            dirty = true;
        }
        if (moveDown >= 0) {
            std::swap(an->frames[size_t(moveDown)],
                      an->frames[size_t(moveDown + 1)]);
            dirty = true;
        }
        if (removeAt >= 0) {
            an->frames.erase(an->frames.begin() + removeAt);
            dirty = true;
        }
        if (frameInsertAt >= 0 && !m_clipboardFrames.empty()) {
            an->frames.insert(an->frames.begin() + frameInsertAt,
                              m_clipboardFrames.begin(),
                              m_clipboardFrames.end());
            dirty = true;
        }
        if (ImGui::Button("+ Frame")) {
            SceneEditor::AnimationFrame nf;
            nf.sprite = subNames.empty() ? std::string{} : subNames.front();
            nf.duration = 0.1f;
            an->frames.push_back(std::move(nf));
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reverse") && an->frames.size() > 1) {
            // Flips the entire frame sequence -- handy for
            // authoring "play backward" variants of an existing
            // animation. Operates on the full vector regardless
            // of the current filter so the result matches what
            // playback would do.
            std::reverse(an->frames.begin(), an->frames.end());
            dirty = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat("##applydur", &m_animEditorApplyDuration,
                         0.01f, 0.01f, 60.0f, "%.3fs");
        ImGui::SameLine();
        if (ImGui::Button("Apply to all") && !an->frames.empty()) {
            // Bulk-set every frame's duration. Useful when the
            // user wants a uniform tempo across a sprite-sheet
            // animation -- the per-row DragFloats stay editable
            // for the rare frame that needs to differ.
            for (auto& fr : an->frames) {
                fr.duration = m_animEditorApplyDuration;
            }
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy##frames") && !an->frames.empty()) {
            m_clipboardFrames = an->frames;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copy %zu frames to clipboard.",
                              an->frames.size());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_clipboardFrames.empty());
        if (ImGui::Button("Paste##frames")) {
            for (const auto& f : m_clipboardFrames) {
                an->frames.push_back(f);
            }
            dirty = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                m_clipboardFrames.empty()
                    ? "Clipboard empty."
                    : "Append %zu frames from clipboard.",
                m_clipboardFrames.size());
        }
        // Drop zone: dragging another .nanim from the Project panel
        // here appends its frames + events to the current asset.
        // Renders a faint outlined rectangle so the affordance is
        // visible even before the user starts a drag.
        {
            const ImVec2 dropMin = ImGui::GetCursorScreenPos();
            const float  dropW   = ImGui::GetContentRegionAvail().x;
            const float  dropH   = 28.0f;
            ImGui::InvisibleButton("##animdrop",
                                    ImVec2(dropW, dropH));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 dropMax{dropMin.x + dropW, dropMin.y + dropH};
            const bool   hovered = ImGui::IsItemHovered();
            const ImU32  border  = hovered
                ? IM_COL32(255, 220, 60, 255)
                : IM_COL32(120, 120, 130, 160);
            dl->AddRect(dropMin, dropMax, border, 4.0f, 0,
                        hovered ? 2.0f : 1.0f);
            const char* hint = "Drop a .nanim here to append its "
                               "frames + events.";
            const ImVec2 textSize = ImGui::CalcTextSize(hint);
            const ImVec2 textPos{
                dropMin.x + (dropW - textSize.x) * 0.5f,
                dropMin.y + (dropH - textSize.y) * 0.5f };
            dl->AddText(textPos,
                        hovered ? IM_COL32(255, 240, 200, 255)
                                : IM_COL32(160, 160, 170, 200),
                        hint);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p =
                        ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                    if (p->DataSize ==
                            sizeof(SceneEditor::AssetGuid)) {
                        SceneEditor::AssetGuid guid;
                        std::memcpy(&guid, p->Data, sizeof(guid));
                        // Skip self-drop -- pasting an asset into
                        // itself would double every row.
                        if (guid != m_animEditorAsset) {
                            if (auto* src = m_assetDb.GetAnimation(guid)) {
                                for (const auto& f : src->frames) {
                                    an->frames.push_back(f);
                                }
                                for (const auto& e : src->events) {
                                    an->events.push_back(e);
                                }
                                dirty = true;
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        // Events: one-shot markers fired when the play cursor
        // crosses `time`. The editor's Console panel logs them; the
        // runtime hook lives with the script system in the next
        // milestone.
        ImGui::Separator();
        ImGui::Text("Events (%zu)", an->events.size());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##eventfilter", "Filter events...",
            m_animEditorEventFilter,
            sizeof(m_animEditorEventFilter));
        // Selection-preserving sort by time. Used by the "Sort by
        // time" button below and -- when the Auto checkbox is on --
        // after any add or per-row time edit.
        auto SortEventsByTime = [&]() {
            if (an->events.size() < 2) return;
            SceneEditor::AnimationEvent selSnapshot;
            bool hadSelection = false;
            if (m_animEditorSelEvent >= 0 &&
                m_animEditorSelEvent < int(an->events.size())) {
                selSnapshot  = an->events[size_t(m_animEditorSelEvent)];
                hadSelection = true;
            }
            std::sort(an->events.begin(), an->events.end(),
                      [](const SceneEditor::AnimationEvent& a,
                         const SceneEditor::AnimationEvent& b) {
                          return a.time < b.time;
                      });
            if (hadSelection) {
                m_animEditorSelEvent = -1;
                for (size_t i = 0; i < an->events.size(); ++i) {
                    const auto& e = an->events[i];
                    if (e.time      == selSnapshot.time &&
                        e.name      == selSnapshot.name &&
                        e.stringArg == selSnapshot.stringArg) {
                        m_animEditorSelEvent = int(i);
                        break;
                    }
                }
            }
            dirty = true;
        };
        bool anyTimeChanged = false;
        int  removeEvAt = -1;
        int  eventInsertAt = -1;
        if (m_animEditorSelEvent >= int(an->events.size())) {
            m_animEditorSelEvent = -1;
        }
        for (size_t i = 0; i < an->events.size(); ++i) {
            SceneEditor::AnimationEvent& ev = an->events[i];
            if (!ParamNameMatchesFilter(
                    ev.name, m_animEditorEventFilter)) {
                continue;
            }
            ImGui::PushID(int(60000 + i));
            if (ImGui::SmallButton("...##evrow")) {
                ImGui::OpenPopup("evrowctx");
            }
            if (ImGui::BeginPopup("evrowctx")) {
                if (ImGui::MenuItem("Copy")) {
                    m_clipboardEvents = { ev };
                }
                ImGui::BeginDisabled(m_clipboardEvents.empty());
                if (ImGui::MenuItem("Paste above")) {
                    eventInsertAt = int(i);
                }
                if (ImGui::MenuItem("Paste below")) {
                    eventInsertAt = int(i) + 1;
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            const bool selRow = (m_animEditorSelEvent == int(i));
            // Jump-to button. Selects the row + parks the scrubber on
            // the event's time, paused, so the preview locks onto
            // whichever frame the user is investigating.
            if (selRow) ImGui::PushStyleColor(ImGuiCol_Button,
                                              IM_COL32(220, 180, 70, 255));
            if (ImGui::SmallButton("->")) {
                m_animEditorSelEvent  = int(i);
                m_animEditorScrubTime = ev.time;
                m_animEditorAutoplay  = false;
            }
            if (selRow) ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(85.0f);
            if (ImGui::DragFloat("##evt", &ev.time, 0.01f,
                                  0.0f, 0.0f, "%.3fs")) {
                dirty = true;
                anyTimeChanged = true;
            }
            ImGui::SameLine();
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", ev.name.c_str());
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::InputText("##evname", nameBuf, sizeof(nameBuf))) {
                ev.name = nameBuf;
                dirty = true;
            }
            ImGui::SameLine();
            char sargBuf[128];
            std::snprintf(sargBuf, sizeof(sargBuf), "%s",
                          ev.stringArg.c_str());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputTextWithHint("##evsarg", "string",
                                          sargBuf, sizeof(sargBuf))) {
                ev.stringArg = sargBuf;
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::DragFloat("##evfarg", &ev.floatArg, 0.05f,
                                  0.0f, 0.0f, "f %.2f")) dirty = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55.0f);
            if (ImGui::DragInt("##eviarg", &ev.intArg, 0.2f,
                                0, 0, "i %d")) dirty = true;
            ImGui::SameLine();
            {
                bool draft = ev.boolArg;
                if (ImGui::Checkbox("##evbarg", &draft)) {
                    ev.boolArg = draft;
                    dirty = true;
                }
            }
            ImGui::SameLine();
            {
                float v2[2] = { ev.vec2Arg.x, ev.vec2Arg.y };
                ImGui::SetNextItemWidth(95.0f);
                if (ImGui::DragFloat2("##evv2arg", v2, 0.05f,
                                       0.0f, 0.0f, "%.2f")) {
                    ev.vec2Arg = { v2[0], v2[1] };
                    dirty = true;
                }
            }
            ImGui::SameLine();
            {
                float col[4] = { ev.colorArg.r, ev.colorArg.g,
                                 ev.colorArg.b, ev.colorArg.a };
                if (ImGui::ColorEdit4("##evcarg", col,
                                       ImGuiColorEditFlags_NoLabel |
                                       ImGuiColorEditFlags_NoInputs |
                                       ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    ev.colorArg = { col[0], col[1], col[2], col[3] };
                    dirty = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeEvAt = int(i);
            ImGui::PopID();
        }
        if (removeEvAt >= 0) {
            an->events.erase(an->events.begin() + removeEvAt);
            dirty = true;
        }
        if (eventInsertAt >= 0 && !m_clipboardEvents.empty()) {
            an->events.insert(an->events.begin() + eventInsertAt,
                              m_clipboardEvents.begin(),
                              m_clipboardEvents.end());
            dirty = true;
            if (m_animEditorAutoSort) {
                SortEventsByTime();
            }
        }
        bool eventAdded = false;
        if (ImGui::Button("+ Event")) {
            SceneEditor::AnimationEvent ev;
            ev.time = 0.0f;
            ev.name = "event";
            an->events.push_back(std::move(ev));
            dirty = true;
            eventAdded = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sort by time")) {
            SortEventsByTime();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto", &m_animEditorAutoSort);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Re-sort the events list by time after every add or "
                "time edit. Selection survives the reorder.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy##events") && !an->events.empty()) {
            m_clipboardEvents = an->events;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copy %zu events to clipboard.",
                              an->events.size());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_clipboardEvents.empty());
        if (ImGui::Button("Paste##events")) {
            for (const auto& e : m_clipboardEvents) {
                an->events.push_back(e);
            }
            dirty = true;
            if (m_animEditorAutoSort) {
                // Newly pasted events likely sit at arbitrary
                // times; if Auto is on, sort once so the rows +
                // markers land in playback order.
                SortEventsByTime();
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                m_clipboardEvents.empty()
                    ? "Clipboard empty."
                    : "Append %zu events from clipboard.",
                m_clipboardEvents.size());
        }
        // Auto-sort trigger: only re-sort when something with a
        // visible time impact actually happened this frame, so
        // non-time edits (name / args) don't churn row order while
        // the user is in the middle of typing.
        if (m_animEditorAutoSort && (anyTimeChanged || eventAdded)) {
            SortEventsByTime();
        }
        ImGui::Separator();

        // Scrubber + autoplay. Editor-local so opening the editor
        // never mutates the live scene's AnimatorComponent.
        const float total = an->TotalDuration();
        if (m_animEditorAutoplay && total > 0.0f) {
            m_animEditorScrubTime += ImGui::GetIO().DeltaTime;
            if (m_animEditorScrubTime > total) {
                m_animEditorScrubTime = an->loop
                    ? std::fmod(m_animEditorScrubTime, total)
                    : total;
            }
        }
        ImGui::SliderFloat("Time", &m_animEditorScrubTime, 0.0f,
                           std::max(0.01f, total), "%.3f s");
        // Event markers under the slider: a small triangle per
        // declared event, brighter on the selected one. Helps the
        // user see which events live near the current scrub cursor
        // without scanning the row list.
        if (!an->events.empty() && total > 0.0f) {
            ImDrawList* tdl = ImGui::GetWindowDrawList();
            const ImVec2 sMin = ImGui::GetItemRectMin();
            const ImVec2 sMax = ImGui::GetItemRectMax();
            const float  sliderW = sMax.x - sMin.x;
            const float  markY   = sMax.y + 2.0f;
            for (size_t i = 0; i < an->events.size(); ++i) {
                const auto& ev = an->events[i];
                const float t  = std::clamp(ev.time, 0.0f, total);
                const float x  = sMin.x + (t / total) * sliderW;
                const bool sel = (m_animEditorSelEvent == int(i));
                const ImU32 col = sel
                    ? IM_COL32(255, 220, 60, 255)
                    : IM_COL32(170, 180, 200, 200);
                const float h = sel ? 9.0f : 7.0f;
                const float w = sel ? 5.0f : 4.0f;
                tdl->AddTriangleFilled(
                    ImVec2(x - w, markY + h),
                    ImVec2(x + w, markY + h),
                    ImVec2(x,     markY),
                    col);
            }
            // Reserve the layout space the markers visually use so
            // following widgets sit below them.
            ImGui::Dummy(ImVec2(0, 12));
        }
        ImGui::SameLine();
        ImGui::Checkbox("Autoplay", &m_animEditorAutoplay);
        const SceneEditor::AnimationFrame* curFr =
            an->Sample(m_animEditorScrubTime);
        ImGui::TextDisabled("Current frame: %s",
                            curFr ? curFr->sprite.c_str() : "(none)");

        // Texture preview with current frame outlined.
        if (an->texture.IsValid()) {
            if (NaraGame::RHITexture* tex =
                    m_assetDb.GetTexture(GetDevice(), an->texture)) {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                const float texW = float(tex->Width());
                const float texH = float(tex->Height());
                float scale = std::min(avail.x, std::min(280.0f, avail.y))
                              / std::max(texW, texH);
                if (scale <= 0.0f) scale = 1.0f;
                const ImVec2 imgSize{texW * scale, texH * scale};
                const ImVec2 imgMin = ImGui::GetCursorScreenPos();
                const ImTextureID texId =
                    static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                        tex->GetNativeView()));
                ImGui::Image(texId, imgSize);
                // The same TextureImportSettings powers both the
                // current-frame highlight and the per-event marker
                // dots below, so load it once.
                SceneEditor::TextureImportSettings imp;
                bool haveImp = false;
                if (const auto* te = m_assetDb.FindByGuid(an->texture)) {
                    imp.FromJson(te->importSettings);
                    haveImp = !imp.spriteSheet.sprites.empty();
                }
                ImDrawList* pdl = ImGui::GetWindowDrawList();
                auto findRect = [&](const std::string& name,
                                    ImVec2& a, ImVec2& b) {
                    for (const auto& s : imp.spriteSheet.sprites) {
                        if (s.name != name) continue;
                        a = ImVec2{imgMin.x + s.x * scale,
                                   imgMin.y + s.y * scale};
                        b = ImVec2{imgMin.x + (s.x + s.w) * scale,
                                   imgMin.y + (s.y + s.h) * scale};
                        return true;
                    }
                    return false;
                };
                if (curFr && haveImp) {
                    ImVec2 a, b;
                    if (findRect(curFr->sprite, a, b)) {
                        pdl->AddRect(a, b, IM_COL32(255, 220, 60, 255),
                                     0.0f, 0, 2.0f);
                    }
                }
                // Event markers: one dot per event in the top-right
                // corner of the sub-rect that fires the event.
                // Multiple events on the same frame stack downward so
                // every dot stays visible. The drawn positions are
                // cached for the overlay hit-test below.
                struct EvtDot { int idx; ImVec2 centre; float radius; };
                std::vector<EvtDot> dots;
                if (haveImp && !an->events.empty() && !an->frames.empty()) {
                    std::unordered_map<std::string, int> perSpriteCount;
                    for (size_t ei = 0; ei < an->events.size(); ++ei) {
                        const auto& ev = an->events[ei];
                        // Locate the frame the event time falls into.
                        float acc = 0.0f;
                        int   frameIdx = -1;
                        for (size_t fi = 0; fi < an->frames.size(); ++fi) {
                            const float next = acc + an->frames[fi].duration;
                            if (ev.time >= acc && ev.time < next) {
                                frameIdx = int(fi);
                                break;
                            }
                            acc = next;
                        }
                        if (frameIdx < 0) continue;
                        const std::string& fs =
                            an->frames[size_t(frameIdx)].sprite;
                        ImVec2 a, b;
                        if (!findRect(fs, a, b)) continue;
                        const float r       = 5.0f;
                        const int   stackI  = perSpriteCount[fs]++;
                        const ImVec2 c{b.x - r - 3.0f,
                                       a.y + r + 3.0f +
                                           stackI * (r * 2.0f + 1.0f)};
                        // Skip dots that would land outside the
                        // sub-rect, so a crowded sprite doesn't get
                        // a column of dots dangling underneath.
                        if (c.y - r > b.y) continue;
                        const bool isSel =
                            (int(ei) == m_animEditorSelEvent);
                        const ImU32 fillCol = isSel
                            ? IM_COL32(255, 220, 60, 255)
                            : IM_COL32(150, 200, 255, 220);
                        pdl->AddCircleFilled(c, r, fillCol);
                        pdl->AddCircle(c, r,
                                       IM_COL32(20, 20, 30, 255),
                                       0, 1.5f);
                        dots.push_back({int(ei), c, r});
                    }
                }

                // Invisible overlay catches clicks + hovers over the
                // image so the dots can act as buttons. ImGui::Image
                // doesn't consume input, so we re-anchor the cursor +
                // drop an InvisibleButton at the same place.
                if (!dots.empty()) {
                    ImGui::SetCursorScreenPos(imgMin);
                    ImGui::InvisibleButton("##animevt_overlay", imgSize,
                                           ImGuiButtonFlags_MouseButtonLeft);
                    if (ImGui::IsItemHovered()) {
                        const ImVec2 mp = ImGui::GetMousePos();
                        int hovered = -1;
                        // Generous hit radius (1.6x) so dots are
                        // forgiving to click without overlapping each
                        // other in tight stacks.
                        for (const EvtDot& d : dots) {
                            const float dx = mp.x - d.centre.x;
                            const float dy = mp.y - d.centre.y;
                            const float rr = d.radius * 1.6f;
                            if (dx * dx + dy * dy <= rr * rr) {
                                hovered = d.idx;
                                break;
                            }
                        }
                        if (hovered >= 0) {
                            const auto& ev =
                                an->events[size_t(hovered)];
                            const std::string args =
                                FormatAnimationEventArgs(ev);
                            if (!args.empty()) {
                                ImGui::SetTooltip(
                                    "\"%s\" @ %.3fs\n%s",
                                    ev.name.c_str(), ev.time,
                                    args.c_str());
                            } else {
                                ImGui::SetTooltip("\"%s\" @ %.3fs",
                                                  ev.name.c_str(), ev.time);
                            }
                            if (ImGui::IsMouseClicked(
                                    ImGuiMouseButton_Left)) {
                                m_animEditorSelEvent  = hovered;
                                m_animEditorScrubTime = ev.time;
                                m_animEditorAutoplay  = false;
                            }
                        }
                    }
                }
            }
        }
        if (dirty) m_assetDb.SaveAnimation(m_animEditorAsset);
        ImGui::End();
    }

    void OpenAutotileEditor(const SceneEditor::AssetGuid& guid) {
        m_autotileEditorAsset    = guid;
        m_autotileEditorOpen     = true;
        m_autotileEditorActivate = true;
        m_autotileEditorSlot     = 0;
    }

    // Autotile Editor window. Left half shows the source texture with a
    // grid overlay -- clicking a cell writes its tile index into the
    // currently active mask slot. Right half lists the 16 mask slots
    // with their neighbour pattern + current tile thumbnail; clicking
    // a row makes it the active slot.
    void DrawAutotileEditorWindow() {
        if (!m_autotileEditorOpen) return;
        if (m_autotileEditorActivate) {
            ImGui::SetNextWindowSize(ImVec2(820, 600), ImGuiCond_Appearing);
            ImGui::SetNextWindowFocus();
            m_autotileEditorActivate = false;
        }
        if (!ImGui::Begin("Autotile Editor", &m_autotileEditorOpen)) {
            ImGui::End();
            return;
        }
        const SceneEditor::AssetEntry* entry =
            m_assetDb.FindByGuid(m_autotileEditorAsset);
        if (!entry || entry->typeName != "Autotile") {
            ImGui::TextDisabled("Double-click a .autotile in the "
                                "Project panel to edit it.");
            ImGui::End();
            return;
        }
        SceneEditor::AutotileAsset* at =
            m_assetDb.GetAutotile(m_autotileEditorAsset);
        if (!at) {
            ImGui::TextDisabled("Could not load '%s'.",
                                entry->relPath.c_str());
            ImGui::End();
            return;
        }
        ImGui::Text("%s", entry->relPath.c_str());

        bool dirty = false;

        // Texture combo.
        std::string current = "(none)";
        if (at->texture.IsValid()) {
            if (const auto* te = m_assetDb.FindByGuid(at->texture)) {
                current = te->relPath;
            } else {
                current = at->texture.ToString();
            }
        }
        if (ImGui::BeginCombo("Texture", current.c_str())) {
            if (ImGui::Selectable("(none)", !at->texture.IsValid())) {
                at->texture = SceneEditor::AssetGuid{};
                dirty = true;
            }
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Texture") continue;
                const bool sel = (a.guid == at->texture);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    at->texture = a.guid;
                    dirty = true;
                }
            }
            ImGui::EndCombo();
        }
        int sz[2] = { at->cellPixelW, at->cellPixelH };
        if (ImGui::DragInt2("Cell Pixels (W,H)", sz, 0.2f, 1, 4096)) {
            at->cellPixelW = std::max(1, sz[0]);
            at->cellPixelH = std::max(1, sz[1]);
            dirty = true;
        }
        const char* modeNames[] = { "Cardinal (4-bit, 16 tiles)",
                                    "Blob (8-bit, 47 tiles)" };
        int modeIdx = int(at->mode);
        if (ImGui::Combo("Mode", &modeIdx, modeNames, 2)) {
            at->mode = static_cast<
                SceneEditor::AutotileAsset::Mode>(modeIdx);
            m_autotileEditorSlot = 0;
            at->RebuildTileSet();
            dirty = true;
        }
        ImGui::Separator();
        if (!at->texture.IsValid()) {
            ImGui::TextDisabled("Pick a Texture above to start editing.");
            if (dirty) m_assetDb.SaveAutotile(m_autotileEditorAsset);
            ImGui::End();
            return;
        }
        NaraGame::RHITexture* tex =
            m_assetDb.GetTexture(GetDevice(), at->texture);
        if (!tex || tex->Width() == 0 || tex->Height() == 0) {
            ImGui::TextDisabled("Texture preview unavailable.");
            if (dirty) m_assetDb.SaveAutotile(m_autotileEditorAsset);
            ImGui::End();
            return;
        }
        const int texCols = int(tex->Width())  / at->cellPixelW;
        const int texRows = int(tex->Height()) / at->cellPixelH;
        if (texCols <= 0 || texRows <= 0) {
            ImGui::TextDisabled("Cell size larger than texture.");
            if (dirty) m_assetDb.SaveAutotile(m_autotileEditorAsset);
            ImGui::End();
            return;
        }
        const bool blob =
            (at->mode == SceneEditor::AutotileAsset::Mode::Blob);
        const int slotCount = blob ? 256 : 16;
        if (m_autotileEditorSlot >= slotCount) m_autotileEditorSlot = 0;
        if (blob) {
            ImGui::TextDisabled("Click a tile to assign it to mask slot "
                                "%d (canonical of the active selection). "
                                "Bits: N=1 NE=2 E=4 SE=8 S=16 SW=32 W=64 "
                                "NW=128.", m_autotileEditorSlot);
        } else {
            ImGui::TextDisabled("Click a tile in the preview to assign "
                                "it to mask slot %d. Bits: N=1, E=2, "
                                "S=4, W=8.", m_autotileEditorSlot);
        }

        // Left: texture preview with grid.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float leftW  = avail.x * 0.55f;
        const float fitH   = avail.y;
        const float texW   = float(tex->Width());
        const float texH   = float(tex->Height());
        float scale = std::min(leftW / texW, fitH / texH);
        if (scale <= 0.0f) scale = 1.0f;
        const ImVec2 imgSize{texW * scale, texH * scale};
        const ImVec2 imgMin = ImGui::GetCursorScreenPos();
        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));
        ImGui::Image(texId, imgSize);
        ImGui::SetCursorScreenPos(imgMin);
        ImGui::InvisibleButton("##autotile_hit", imgSize,
                               ImGuiButtonFlags_MouseButtonLeft);
        const bool clicked = ImGui::IsItemClicked();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 grid = IM_COL32(255, 255, 255, 50);
        const float cellPxScreenW = float(at->cellPixelW) * scale;
        const float cellPxScreenH = float(at->cellPixelH) * scale;
        for (int r = 0; r <= texRows; ++r) {
            const float y = imgMin.y + r * cellPxScreenH;
            dl->AddLine(ImVec2{imgMin.x,             y},
                        ImVec2{imgMin.x + imgSize.x, y}, grid, 1.0f);
        }
        for (int cc = 0; cc <= texCols; ++cc) {
            const float x = imgMin.x + cc * cellPxScreenW;
            dl->AddLine(ImVec2{x, imgMin.y},
                        ImVec2{x, imgMin.y + imgSize.y}, grid, 1.0f);
        }
        // Highlight the cell currently assigned to the active slot.
        if (m_autotileEditorSlot >= 0 && m_autotileEditorSlot < slotCount) {
            const int idx = blob
                ? at->blobMask[size_t(m_autotileEditorSlot)]
                : at->mask[size_t(m_autotileEditorSlot)];
            if (idx >= 0 && idx < texCols * texRows) {
                const int col = idx % texCols;
                const int row = idx / texCols;
                const ImVec2 a{imgMin.x +  col      * cellPxScreenW,
                               imgMin.y +  row      * cellPxScreenH};
                const ImVec2 b{imgMin.x + (col + 1) * cellPxScreenW,
                               imgMin.y + (row + 1) * cellPxScreenH};
                dl->AddRect(a, b, IM_COL32(255, 220, 60, 255),
                            0.0f, 0, 2.0f);
            }
        }
        if (clicked) {
            const ImVec2 mp = ImGui::GetMousePos();
            const int px = int((mp.x - imgMin.x) / scale);
            const int py = int((mp.y - imgMin.y) / scale);
            const int col = px / at->cellPixelW;
            const int row = py / at->cellPixelH;
            if (col >= 0 && col < texCols && row >= 0 && row < texRows) {
                const int idx = row * texCols + col;
                if (blob) {
                    // Write to ALL non-canonical slots that share this
                    // canonical entry so the direct lookup table stays
                    // consistent.
                    const int canon = SceneEditor::AutotileAsset::
                        Canonicalize8bit(m_autotileEditorSlot);
                    if (at->blobMask[size_t(canon)] != idx) {
                        for (int m = 0; m < 256; ++m) {
                            if (SceneEditor::AutotileAsset::
                                    Canonicalize8bit(m) == canon) {
                                at->blobMask[size_t(m)] = idx;
                            }
                        }
                        at->RebuildTileSet();
                        dirty = true;
                    }
                    // Advance to the next canonical slot so the user
                    // can rip through the 47-entry blob without
                    // grabbing the slot list.
                    do {
                        m_autotileEditorSlot =
                            (m_autotileEditorSlot + 1) & 0xFF;
                    } while (!SceneEditor::AutotileAsset::
                                IsCanonical8bit(m_autotileEditorSlot) &&
                             m_autotileEditorSlot != 0);
                } else {
                    if (at->mask[size_t(m_autotileEditorSlot)] != idx) {
                        at->mask[size_t(m_autotileEditorSlot)] = idx;
                        at->RebuildTileSet();
                        dirty = true;
                    }
                    m_autotileEditorSlot =
                        (m_autotileEditorSlot + 1) & 15;
                }
            }
        }

        // Right: slot table. Cardinal lists all 16; Blob skips
        // non-canonical entries so the user only sees the ~47 unique
        // tile slots that actually need authoring.
        ImGui::SameLine();
        ImGui::BeginChild("##autotile_slots", ImVec2(0, fitH), true);
        ImGui::Text("Mask slots (%s)",
                    blob ? "Blob, ~47 canonical"
                         : "Cardinal, 16");
        ImGui::Separator();
        for (int m = 0; m < slotCount; ++m) {
            if (blob && !SceneEditor::AutotileAsset::IsCanonical8bit(m))
                continue;
            ImGui::PushID(m);
            const int  tile = blob ? at->blobMask[size_t(m)]
                                   : at->mask[size_t(m)];
            const bool sel  = (m_autotileEditorSlot == m);
            char label[96];
            if (blob) {
                std::snprintf(label, sizeof(label),
                              "%3d  %c%c%c%c%c%c%c%c  -> tile %d", m,
                              (m & 1)   ? 'N' : '.',
                              (m & 2)   ? 'e' : '.',
                              (m & 4)   ? 'E' : '.',
                              (m & 8)   ? 's' : '.',
                              (m & 16)  ? 'S' : '.',
                              (m & 32)  ? 'w' : '.',
                              (m & 64)  ? 'W' : '.',
                              (m & 128) ? 'n' : '.',
                              tile);
            } else {
                std::snprintf(label, sizeof(label),
                              "%2d  %c%c%c%c  -> tile %d", m,
                              (m & 1) ? 'N' : '.',
                              (m & 2) ? 'E' : '.',
                              (m & 4) ? 'S' : '.',
                              (m & 8) ? 'W' : '.',
                              tile);
            }
            if (ImGui::Selectable(label, sel)) m_autotileEditorSlot = m;
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (dirty) m_assetDb.SaveAutotile(m_autotileEditorAsset);
        ImGui::End();
    }

    void OpenTilesetEditor(const SceneEditor::AssetGuid& guid) {
        m_tilesetEditorAsset    = guid;
        m_tilesetEditorOpen     = true;
        m_tilesetEditorActivate = true;
    }

    // Tileset Editor window. Lets the user pick the source texture, set
    // cell size, and per-tile flag tiles as "collides" via a click on
    // the preview grid. Auto-saves to the .ntileset file on every edit.
    void DrawTilesetEditorWindow() {
        if (!m_tilesetEditorOpen) return;
        if (m_tilesetEditorActivate) {
            ImGui::SetNextWindowSize(ImVec2(720, 540), ImGuiCond_Appearing);
            ImGui::SetNextWindowFocus();
            m_tilesetEditorActivate = false;
        }
        if (!ImGui::Begin("Tileset Editor", &m_tilesetEditorOpen)) {
            ImGui::End();
            return;
        }
        const SceneEditor::AssetEntry* entry =
            m_assetDb.FindByGuid(m_tilesetEditorAsset);
        if (!entry || entry->typeName != "Tileset") {
            ImGui::TextDisabled("Double-click a .ntileset in the Project "
                                "panel to edit it.");
            ImGui::End();
            return;
        }
        SceneEditor::TilesetAsset* ts =
            m_assetDb.GetTileset(m_tilesetEditorAsset);
        if (!ts) {
            ImGui::TextDisabled("Could not load '%s'.",
                                entry->relPath.c_str());
            ImGui::End();
            return;
        }
        ImGui::Text("%s", entry->relPath.c_str());

        bool dirty = false;

        // Texture combo.
        std::string current = "(none)";
        if (ts->texture.IsValid()) {
            if (const auto* te = m_assetDb.FindByGuid(ts->texture)) {
                current = te->relPath;
            } else {
                current = ts->texture.ToString();
            }
        }
        if (ImGui::BeginCombo("Texture", current.c_str())) {
            if (ImGui::Selectable("(none)", !ts->texture.IsValid())) {
                ts->texture = SceneEditor::AssetGuid{};
                dirty = true;
            }
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Texture") continue;
                const bool sel = (a.guid == ts->texture);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    ts->texture = a.guid;
                    dirty = true;
                }
            }
            ImGui::EndCombo();
        }
        int sz[2] = { ts->cellPixelW, ts->cellPixelH };
        if (ImGui::DragInt2("Cell Pixels (W,H)", sz, 0.2f, 1, 4096)) {
            ts->cellPixelW = std::max(1, sz[0]);
            ts->cellPixelH = std::max(1, sz[1]);
            dirty = true;
        }
        ImGui::Separator();

        if (!ts->texture.IsValid()) {
            ImGui::TextDisabled("Pick a Texture above to start tagging tiles.");
            if (dirty) m_assetDb.SaveTileset(m_tilesetEditorAsset);
            ImGui::End();
            return;
        }
        NaraGame::RHITexture* tex =
            m_assetDb.GetTexture(GetDevice(), ts->texture);
        if (!tex || tex->Width() == 0 || tex->Height() == 0) {
            ImGui::TextDisabled("Texture preview unavailable.");
            if (dirty) m_assetDb.SaveTileset(m_tilesetEditorAsset);
            ImGui::End();
            return;
        }
        const int texCols = int(tex->Width())  / ts->cellPixelW;
        const int texRows = int(tex->Height()) / ts->cellPixelH;
        ImGui::TextDisabled("%d x %d tiles  -- click cycles "
                            "blank -> Box -> OneWay -> SlopeR/L "
                            "(45/22.5) -> Trigger -> blank",
                            texCols, texRows);

        // Preview area.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float texW = float(tex->Width());
        const float texH = float(tex->Height());
        float scale = std::min(avail.x / texW, avail.y / texH);
        if (scale <= 0.0f) scale = 1.0f;
        const ImVec2 imgSize{texW * scale, texH * scale};
        const ImVec2 imgMin = ImGui::GetCursorScreenPos();
        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));
        ImGui::Image(texId, imgSize);
        ImGui::SetCursorScreenPos(imgMin);
        ImGui::InvisibleButton("##tileset_hit", imgSize,
                               ImGuiButtonFlags_MouseButtonLeft);
        const bool clicked = ImGui::IsItemClicked();
        const float cellScreenW = float(ts->cellPixelW) * scale;
        const float cellScreenH = float(ts->cellPixelH) * scale;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 grid = IM_COL32(255, 255, 255, 50);
        for (int r = 0; r <= texRows; ++r) {
            const float y = imgMin.y + r * cellScreenH;
            dl->AddLine(ImVec2{imgMin.x,             y},
                        ImVec2{imgMin.x + imgSize.x, y}, grid, 1.0f);
        }
        for (int c = 0; c <= texCols; ++c) {
            const float x = imgMin.x + c * cellScreenW;
            dl->AddLine(ImVec2{x, imgMin.y},
                        ImVec2{x, imgMin.y + imgSize.y}, grid, 1.0f);
        }
        // Highlight cells by mode:
        //   Solid Box     = red    (M4 colour)
        //   Solid OneWay  = orange (visually closer to solid +
        //                            cues the "directional" intent)
        //   Trigger       = blue   (M7c19 colour)
        // Cells with no flag set don't draw an overlay.
        for (const auto& kv : ts->tiles) {
            if (!kv.second.collides && !kv.second.isTrigger) continue;
            const int idx = kv.first;
            const int col = idx % texCols;
            const int row = idx / texCols;
            if (row >= texRows) continue;
            const ImVec2 a{imgMin.x +  col      * cellScreenW,
                           imgMin.y +  row      * cellScreenH};
            const ImVec2 b{imgMin.x + (col + 1) * cellScreenW,
                           imgMin.y + (row + 1) * cellScreenH};
            ImU32 fill, edge;
            if (kv.second.isTrigger) {
                fill = IM_COL32( 60, 140, 220,  90);
                edge = IM_COL32(100, 170, 255, 255);
            } else if (kv.second.shape == SceneEditor::TileColliderShape::OneWay) {
                fill = IM_COL32(230, 150,  40,  90);
                edge = IM_COL32(255, 180,  80, 255);
            } else {
                fill = IM_COL32(220,  60,  60,  90);
                edge = IM_COL32(255, 100, 100, 255);
            }
            // Slope tiles draw as triangles or trapezoids that
            // match the filled portion of the cell. High
            // variants are 4-vertex trapezoids; Half + 45-deg
            // variants are 3-vertex triangles. Trigger + Box +
            // OneWay draw as a full rect.
            using ShapeT = SceneEditor::TileColliderShape;
            const auto shape = kv.second.shape;
            const float midY = (a.y + b.y) * 0.5f;
            ImVec2 poly[4];
            int polyCount = 0;
            const bool slopeMode = !kv.second.isTrigger;
            if (slopeMode && shape == ShapeT::SlopeRight) {
                poly[0] = {a.x, b.y}; poly[1] = {b.x, b.y};
                poly[2] = {b.x, a.y};
                polyCount = 3;
            } else if (slopeMode && shape == ShapeT::SlopeLeft) {
                poly[0] = {a.x, b.y}; poly[1] = {b.x, b.y};
                poly[2] = {a.x, a.y};
                polyCount = 3;
            } else if (slopeMode && shape == ShapeT::SlopeRightHalf) {
                poly[0] = {a.x, b.y}; poly[1] = {b.x, b.y};
                poly[2] = {b.x, midY};
                polyCount = 3;
            } else if (slopeMode && shape == ShapeT::SlopeLeftHalf) {
                poly[0] = {a.x, b.y}; poly[1] = {b.x, b.y};
                poly[2] = {a.x, midY};
                polyCount = 3;
            } else if (slopeMode && shape == ShapeT::SlopeRightHigh) {
                poly[0] = {a.x, b.y};   poly[1] = {b.x, b.y};
                poly[2] = {b.x, a.y};   poly[3] = {a.x, midY};
                polyCount = 4;
            } else if (slopeMode && shape == ShapeT::SlopeLeftHigh) {
                poly[0] = {a.x, b.y};   poly[1] = {b.x, b.y};
                poly[2] = {b.x, midY};  poly[3] = {a.x, a.y};
                polyCount = 4;
            }
            if (polyCount > 0) {
                dl->AddConvexPolyFilled(poly, polyCount, fill);
                dl->AddPolyline(poly, polyCount, edge,
                                ImDrawFlags_Closed, 2.0f);
            } else {
                dl->AddRectFilled(a, b, fill);
                dl->AddRect(a, b, edge, 0.0f, 0, 2.0f);
            }
        }

        if (clicked) {
            const ImVec2 mp = ImGui::GetMousePos();
            const int px = int((mp.x - imgMin.x) / scale);
            const int py = int((mp.y - imgMin.y) / scale);
            const int col = px / ts->cellPixelW;
            const int row = py / ts->cellPixelH;
            if (col >= 0 && col < texCols && row >= 0 && row < texRows) {
                // M7c22: 6-state cycle blank -> Box -> OneWay
                // -> SlopeRight -> SlopeLeft -> Trigger -> blank.
                // The runtime branches on the composed flags
                // (collides + isTrigger + shape) without
                // per-cell branching outside this helper.
                const int idx = row * texCols + col;
                auto it = ts->tiles.find(idx);
                using Shape = SceneEditor::TileColliderShape;
                if (it == ts->tiles.end()) {
                    SceneEditor::TilesetTile t;
                    t.collides  = true;
                    t.isTrigger = false;
                    t.shape     = Shape::Box;
                    ts->tiles[idx] = t;
                } else if (!it->second.isTrigger &&
                           it->second.shape == Shape::Box) {
                    it->second.shape = Shape::OneWay;
                } else if (!it->second.isTrigger &&
                           it->second.shape == Shape::OneWay) {
                    it->second.shape = Shape::SlopeRight;
                } else if (!it->second.isTrigger &&
                           it->second.shape == Shape::SlopeRight) {
                    it->second.shape = Shape::SlopeLeft;
                } else if (!it->second.isTrigger &&
                           it->second.shape == Shape::SlopeLeft) {
                    it->second.shape = Shape::SlopeRightHalf;
                } else if (!it->second.isTrigger &&
                           it->second.shape == Shape::SlopeRightHalf) {
                    it->second.shape = Shape::SlopeLeftHalf;
                } else if (!it->second.isTrigger &&
                           it->second.shape == Shape::SlopeLeftHalf) {
                    it->second.shape = Shape::SlopeRightHigh;
                } else if (!it->second.isTrigger &&
                           it->second.shape == Shape::SlopeRightHigh) {
                    it->second.shape = Shape::SlopeLeftHigh;
                } else if (!it->second.isTrigger) {
                    // SlopeLeftHigh -> Trigger (Box trigger).
                    it->second.isTrigger = true;
                    it->second.shape = Shape::Box;
                } else {
                    ts->tiles.erase(it);
                }
                dirty = true;
            }
        }

        if (dirty) m_assetDb.SaveTileset(m_tilesetEditorAsset);
        ImGui::End();
    }

    void DrawSpriteEditorWindow() {
        if (!m_spriteEditorOpen) return;
        if (m_spriteEditorActivate) {
            ImGui::SetNextWindowSize(ImVec2(840, 620), ImGuiCond_Appearing);
            ImGui::SetNextWindowFocus();
            m_spriteEditorActivate = false;
        }
        if (!ImGui::Begin("Sprite Editor", &m_spriteEditorOpen)) {
            ImGui::End();
            return;
        }
        SceneEditor::AssetEntry* entry =
            m_assetDb.FindByGuidMutable(m_spriteEditorAsset);
        if (!entry || entry->typeName != "Texture") {
            ImGui::TextDisabled("Double-click a Texture asset in the "
                                "Project panel to edit it.");
            ImGui::End();
            return;
        }
        NaraGame::RHITexture* tex =
            m_assetDb.GetTexture(GetDevice(), entry->guid);
        if (!tex) {
            ImGui::TextDisabled("Could not load '%s'.",
                                entry->relPath.c_str());
            ImGui::End();
            return;
        }
        ImGui::Text("%s   %ux%u",
                    entry->relPath.c_str(), tex->Width(), tex->Height());

        SceneEditor::TextureImportSettings imp;
        imp.FromJson(entry->importSettings);

        bool dirty = false;

        // Slice mode chooser + parameter inputs.
        const char* sliceTabs[] = { "By Cell Size", "By Cell Count", "Manual" };
        ImGui::Combo("##slicemode", &m_spriteEditorMode, sliceTabs, 3);
        ImGui::SameLine();
        ImGui::TextDisabled("(slice mode)");

        if (m_spriteEditorMode == 0) {
            ImGui::DragInt("Cell Width",  &m_spriteEditorCellW, 1.0f,
                           1, int(tex->Width()));
            ImGui::DragInt("Cell Height", &m_spriteEditorCellH, 1.0f,
                           1, int(tex->Height()));
            if (ImGui::Button("Slice")) {
                imp.spriteMode = SceneEditor::TextureImportSettings::SpriteMode::Multiple;
                imp.spriteSheet.sprites = SceneEditor::SliceByCellSize(
                    int(tex->Width()), int(tex->Height()),
                    m_spriteEditorCellW, m_spriteEditorCellH,
                    FileStem(entry->relPath));
                dirty = true;
            }
        } else if (m_spriteEditorMode == 1) {
            ImGui::DragInt("Columns", &m_spriteEditorCols, 1.0f, 1, 64);
            ImGui::DragInt("Rows",    &m_spriteEditorRows, 1.0f, 1, 64);
            if (ImGui::Button("Slice")) {
                imp.spriteMode = SceneEditor::TextureImportSettings::SpriteMode::Multiple;
                imp.spriteSheet.sprites = SceneEditor::SliceByCellCount(
                    int(tex->Width()), int(tex->Height()),
                    m_spriteEditorCols, m_spriteEditorRows,
                    FileStem(entry->relPath));
                dirty = true;
            }
        } else {
            ImGui::TextWrapped("Click + drag on the preview to add a "
                               "rectangle. Click an existing one to "
                               "select; drag to move it.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Single")) {
            imp.spriteMode = SceneEditor::TextureImportSettings::SpriteMode::Single;
            imp.spriteSheet.sprites.clear();
            m_spriteEditorSel = -1;
            m_manualDrag = ManualDrag::None;
            dirty = true;
        }
        ImGui::Separator();

        // Texture preview area: scale-to-fit while keeping aspect.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float previewMaxW = avail.x * 0.62f;
        const float previewMaxH = avail.y;
        const float texW = float(tex->Width());
        const float texH = float(tex->Height());
        float scale = std::min(previewMaxW / texW, previewMaxH / texH);
        if (scale <= 0.0f) scale = 1.0f;
        const ImVec2 imgSize{texW * scale, texH * scale};

        ImGui::BeginChild("##preview", ImVec2(previewMaxW, previewMaxH),
                          true);
        const ImVec2 imgMin = ImGui::GetCursorScreenPos();
        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));
        ImGui::Image(texId, imgSize);

        // Decide which region's borders are currently editable. A
        // selected sub-sprite wins; otherwise the whole texture's
        // border (Single mode) is exposed. Multiple-mode without a
        // selection leaves the borders read-only -- editing per-sprite
        // borders only makes sense when one of them is picked.
        const BorderRegion br =
            GetActiveBorderRegion(imp, int(tex->Width()), int(tex->Height()));
        const bool wantsInput = br.active || m_spriteEditorMode == 2;
        if (wantsInput) {
            ImGui::SetCursorScreenPos(imgMin);
            ImGui::InvisibleButton("##spreview_hit", imgSize,
                                   ImGuiButtonFlags_MouseButtonLeft);
            // Border-line drag has priority over the Manual create /
            // move flow so a near-line click never lands on the rect
            // underneath by accident.
            bool consumedByBorder = false;
            if (br.active) {
                if (HandleBorderInput(imgMin, scale, br,
                                      int(tex->Width()),
                                      int(tex->Height()),
                                      &consumedByBorder)) {
                    dirty = true;
                }
            }
            if (m_spriteEditorMode == 2 && !consumedByBorder &&
                m_borderDrag == BorderDragSide::None) {
                if (HandleManualSliceInput(imgMin, scale, imp,
                                           int(tex->Width()),
                                           int(tex->Height()),
                                           FileStem(entry->relPath))) {
                    dirty = true;
                }
            }
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DrawSpritePreviewOverlay(dl, imgMin, scale, imp);
        if (br.active) {
            DrawBorderGuides(dl, imgMin, scale, br);
        }
        // In-progress rubber-band rect while the user is drawing a new
        // sub-sprite. Snap to integer pixels so the live preview matches
        // what we eventually persist.
        if (m_spriteEditorMode == 2 && m_manualDrag == ManualDrag::Create) {
            const int x0 = std::min(m_manualAnchorPx[0], m_manualCurPx[0]);
            const int y0 = std::min(m_manualAnchorPx[1], m_manualCurPx[1]);
            const int x1 = std::max(m_manualAnchorPx[0], m_manualCurPx[0]);
            const int y1 = std::max(m_manualAnchorPx[1], m_manualCurPx[1]);
            const ImVec2 a{imgMin.x + x0 * scale, imgMin.y + y0 * scale};
            const ImVec2 b{imgMin.x + x1 * scale, imgMin.y + y1 * scale};
            dl->AddRectFilled(a, b, IM_COL32(80, 200, 255, 60));
            dl->AddRect(a, b, IM_COL32(80, 200, 255, 255), 0.0f, 0, 2.0f);
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // Sub-sprite list on the right.
        ImGui::BeginChild("##sublist", ImVec2(0, previewMaxH), true);
        ImGui::Text("Sub-sprites: %zu", imp.spriteSheet.sprites.size());
        ImGui::SameLine();
        const bool canDelete = (m_spriteEditorSel >= 0 &&
                                m_spriteEditorSel <
                                    int(imp.spriteSheet.sprites.size()));
        if (!canDelete) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Delete Selected")) {
            imp.spriteSheet.sprites.erase(
                imp.spriteSheet.sprites.begin() + m_spriteEditorSel);
            m_spriteEditorSel = -1;
            dirty = true;
        }
        if (!canDelete) ImGui::EndDisabled();
        ImGui::Separator();
        for (size_t i = 0; i < imp.spriteSheet.sprites.size(); ++i) {
            SceneEditor::SubSprite& s = imp.spriteSheet.sprites[i];
            ImGui::PushID(int(i));
            const bool sel = (m_spriteEditorSel == int(i));
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", s.name.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
                s.name = nameBuf;
                dirty = true;
            }
            if (ImGui::IsItemActive() || ImGui::IsItemClicked()) {
                m_spriteEditorSel = int(i);
            }
            const ImU32 rectCol = sel ? IM_COL32(255, 220, 80, 255)
                                      : IM_COL32(170, 170, 170, 255);
            ImGui::PushStyleColor(ImGuiCol_Text, rectCol);
            ImGui::Text("  rect (%d, %d, %d, %d)%s",
                        s.x, s.y, s.w, s.h, sel ? "  *" : "");
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked()) m_spriteEditorSel = int(i);

            // Pivot + 9-slice border for the selected sub-sprite. We
            // only show them on the active row so the list stays
            // scannable when sliced into a lot of cells.
            if (sel) {
                float pv[2] = { s.pivot.x, s.pivot.y };
                if (ImGui::DragFloat2("  pivot", pv, 0.01f, 0.0f, 1.0f)) {
                    s.pivot = { pv[0], pv[1] };
                    dirty = true;
                }
                int border[4] = { s.borderLeft, s.borderRight,
                                  s.borderTop,  s.borderBottom };
                if (ImGui::DragInt4("  border", border, 0.2f, 0, 4096)) {
                    s.borderLeft   = border[0];
                    s.borderRight  = border[1];
                    s.borderTop    = border[2];
                    s.borderBottom = border[3];
                    dirty = true;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (dirty) {
            entry->importSettings = imp.ToJson();
            m_assetDb.SaveMeta(entry->guid);
        }

        ImGui::End();
    }

    void DrawSpritePreviewOverlay(
        ImDrawList* dl, const ImVec2& imgMin, float scale,
        const SceneEditor::TextureImportSettings& imp) const {
        // Each sub-sprite rect drawn over the image at the same scale.
        // Selected sub-sprite gets a bright outline + filled tint.
        for (size_t i = 0; i < imp.spriteSheet.sprites.size(); ++i) {
            const auto& s = imp.spriteSheet.sprites[i];
            const ImVec2 a{imgMin.x + s.x * scale, imgMin.y + s.y * scale};
            const ImVec2 b{imgMin.x + (s.x + s.w) * scale,
                           imgMin.y + (s.y + s.h) * scale};
            const bool sel = (int(i) == m_spriteEditorSel);
            const ImU32 col = sel ? IM_COL32(255, 230, 80, 255)
                                  : IM_COL32(255, 255, 255, 180);
            if (sel) {
                dl->AddRectFilled(a, b, IM_COL32(255, 230, 80, 50), 0.0f);
            }
            dl->AddRect(a, b, col, 0.0f, 0, sel ? 2.0f : 1.0f);
        }
    }

    static std::string FileStem(const std::string& relPath) {
        std::string stem = relPath;
        const size_t slash = stem.find_last_of('/');
        if (slash != std::string::npos) stem = stem.substr(slash + 1);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos) stem.resize(dot);
        return stem;
    }

    // Manual-mode interaction state. Create = the user is dragging out
    // a fresh rect from an empty area; Move = the user is dragging the
    // selected sub-sprite around.
    enum class ManualDrag { None, Create, Move };

    // Which 9-slice border line the user is hovering / dragging on the
    // preview. The four lines are positioned at srcX+L, srcX+srcW-R,
    // srcY+T, srcY+srcH-B inside the active region.
    enum class BorderDragSide { None, Left, Right, Top, Bottom };

    // Pointer into either a selected SubSprite or the texture-level
    // import settings, depending on what the user is currently
    // editing. m_spriteEditorSel wins when valid; otherwise the whole
    // texture's borders surface for Single-mode assets.
    struct BorderRegion {
        bool active = false;
        int  srcX = 0, srcY = 0, srcW = 0, srcH = 0;
        int* L = nullptr;
        int* R = nullptr;
        int* T = nullptr;
        int* B = nullptr;
    };

    BorderRegion GetActiveBorderRegion(
        SceneEditor::TextureImportSettings& imp,
        int texW, int texH) {
        BorderRegion r;
        if (m_spriteEditorSel >= 0 &&
            m_spriteEditorSel < int(imp.spriteSheet.sprites.size())) {
            auto& s = imp.spriteSheet.sprites[size_t(m_spriteEditorSel)];
            r.active = true;
            r.srcX = s.x; r.srcY = s.y;
            r.srcW = s.w; r.srcH = s.h;
            r.L = &s.borderLeft;   r.R = &s.borderRight;
            r.T = &s.borderTop;    r.B = &s.borderBottom;
        } else if (imp.spriteMode ==
                   SceneEditor::TextureImportSettings::SpriteMode::Single) {
            r.active = true;
            r.srcX = 0; r.srcY = 0;
            r.srcW = texW; r.srcH = texH;
            r.L = &imp.borderLeft;  r.R = &imp.borderRight;
            r.T = &imp.borderTop;   r.B = &imp.borderBottom;
        }
        return r;
    }

    void DrawBorderGuides(ImDrawList* dl, const ImVec2& imgMin, float scale,
                          const BorderRegion& br) const {
        if (!br.active) return;
        // Outline the active region in a faint cyan so the user sees
        // which box the border lines belong to (especially helpful when
        // editing a sub-sprite's border inside a larger texture).
        const float x0 = imgMin.x + float(br.srcX) * scale;
        const float y0 = imgMin.y + float(br.srcY) * scale;
        const float x1 = imgMin.x + float(br.srcX + br.srcW) * scale;
        const float y1 = imgMin.y + float(br.srcY + br.srcH) * scale;
        dl->AddRect(ImVec2{x0, y0}, ImVec2{x1, y1},
                    IM_COL32(80, 200, 220, 90), 0.0f, 0, 1.0f);

        auto sideCol = [&](BorderDragSide s) {
            const bool active = (m_borderDrag == s);
            const bool hover  = (m_borderHover == s);
            if (active) return IM_COL32(120, 240, 255, 255);
            if (hover)  return IM_COL32( 90, 220, 255, 255);
            return IM_COL32(60, 180, 220, 200);
        };
        auto thick = [&](BorderDragSide s) {
            if (m_borderDrag == s)  return 3.0f;
            if (m_borderHover == s) return 2.5f;
            return 1.5f;
        };

        const float lx = imgMin.x + float(br.srcX + *br.L)            * scale;
        const float rx = imgMin.x + float(br.srcX + br.srcW - *br.R)  * scale;
        const float ty = imgMin.y + float(br.srcY + *br.T)            * scale;
        const float by = imgMin.y + float(br.srcY + br.srcH - *br.B)  * scale;
        dl->AddLine(ImVec2{lx, y0}, ImVec2{lx, y1},
                    sideCol(BorderDragSide::Left),   thick(BorderDragSide::Left));
        dl->AddLine(ImVec2{rx, y0}, ImVec2{rx, y1},
                    sideCol(BorderDragSide::Right),  thick(BorderDragSide::Right));
        dl->AddLine(ImVec2{x0, ty}, ImVec2{x1, ty},
                    sideCol(BorderDragSide::Top),    thick(BorderDragSide::Top));
        dl->AddLine(ImVec2{x0, by}, ImVec2{x1, by},
                    sideCol(BorderDragSide::Bottom), thick(BorderDragSide::Bottom));
    }

    // Hit-test the four border lines + drive the drag. *consumed is set
    // to true on any click that lands on a line so the Manual flow
    // doesn't double-handle the same click.
    bool HandleBorderInput(const ImVec2& imgMin, float scale,
                           const BorderRegion& br, int texW, int texH,
                           bool* consumed) {
        if (!br.active) return false;
        const ImVec2 mp = ImGui::GetMousePos();
        const float lx = imgMin.x + float(br.srcX + *br.L)            * scale;
        const float rx = imgMin.x + float(br.srcX + br.srcW - *br.R)  * scale;
        const float ty = imgMin.y + float(br.srcY + *br.T)            * scale;
        const float by = imgMin.y + float(br.srcY + br.srcH - *br.B)  * scale;
        const float x0 = imgMin.x + float(br.srcX)                * scale;
        const float y0 = imgMin.y + float(br.srcY)                * scale;
        const float x1 = imgMin.x + float(br.srcX + br.srcW)      * scale;
        const float y1 = imgMin.y + float(br.srcY + br.srcH)      * scale;
        constexpr float kPick = 4.0f;   // hover tolerance in screen px

        // Hover detection. We only consider the cursor "near" a line if
        // it's also within the active region's perpendicular span, so
        // far-away clicks don't latch onto the wrong axis.
        BorderDragSide hover = BorderDragSide::None;
        if (ImGui::IsItemHovered()) {
            if (mp.y >= y0 - kPick && mp.y <= y1 + kPick) {
                if (std::abs(mp.x - lx) < kPick) hover = BorderDragSide::Left;
                else if (std::abs(mp.x - rx) < kPick) hover = BorderDragSide::Right;
            }
            if (hover == BorderDragSide::None &&
                mp.x >= x0 - kPick && mp.x <= x1 + kPick) {
                if (std::abs(mp.y - ty) < kPick) hover = BorderDragSide::Top;
                else if (std::abs(mp.y - by) < kPick) hover = BorderDragSide::Bottom;
            }
        }
        m_borderHover = hover;

        if (m_borderDrag == BorderDragSide::None && hover != BorderDragSide::None &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_borderDrag = hover;
            *consumed = true;
        }

        bool changed = false;
        if (m_borderDrag != BorderDragSide::None &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const int mpx = int(std::clamp((mp.x - imgMin.x) / scale,
                                            0.0f, float(texW)));
            const int mpy = int(std::clamp((mp.y - imgMin.y) / scale,
                                            0.0f, float(texH)));
            int next = 0;
            switch (m_borderDrag) {
                case BorderDragSide::Left:
                    next = std::clamp(mpx - br.srcX, 0, br.srcW - *br.R);
                    if (next != *br.L) { *br.L = next; changed = true; }
                    break;
                case BorderDragSide::Right:
                    next = std::clamp(br.srcX + br.srcW - mpx,
                                      0, br.srcW - *br.L);
                    if (next != *br.R) { *br.R = next; changed = true; }
                    break;
                case BorderDragSide::Top:
                    next = std::clamp(mpy - br.srcY, 0, br.srcH - *br.B);
                    if (next != *br.T) { *br.T = next; changed = true; }
                    break;
                case BorderDragSide::Bottom:
                    next = std::clamp(br.srcY + br.srcH - mpy,
                                      0, br.srcH - *br.T);
                    if (next != *br.B) { *br.B = next; changed = true; }
                    break;
                default: break;
            }
            *consumed = true;
        }
        if (m_borderDrag != BorderDragSide::None &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_borderDrag = BorderDragSide::None;
        }
        return changed;
    }

    // Drive the Manual slice mode's drag-create + drag-move flow. The
    // preview's InvisibleButton must already be the last item submitted
    // when this is called. Returns true when the sprite-sheet changed
    // so the caller saves the .meta.
    bool HandleManualSliceInput(const ImVec2& imgMin, float scale,
                                SceneEditor::TextureImportSettings& imp,
                                int texW, int texH,
                                const std::string& stem) {
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 mp    = ImGui::GetMousePos();
        const int   mpx    = int(std::clamp((mp.x - imgMin.x) / scale,
                                            0.0f, float(texW)));
        const int   mpy    = int(std::clamp((mp.y - imgMin.y) / scale,
                                            0.0f, float(texH)));

        bool changed = false;
        const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool clk  = hovered &&
                          ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool rel  = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        if (clk) {
            // Hit-test existing sub-sprites in reverse so the topmost
            // (drawn last) wins on overlap, matching the viewport's
            // entity hit ordering.
            int hit = -1;
            for (int i = int(imp.spriteSheet.sprites.size()) - 1; i >= 0; --i) {
                const auto& s = imp.spriteSheet.sprites[i];
                if (mpx >= s.x && mpx < s.x + s.w &&
                    mpy >= s.y && mpy < s.y + s.h) {
                    hit = i;
                    break;
                }
            }
            if (hit >= 0) {
                m_spriteEditorSel = hit;
                m_manualDrag      = ManualDrag::Move;
                const auto& s = imp.spriteSheet.sprites[hit];
                m_manualAnchorPx[0] = mpx - s.x;   // grab offset
                m_manualAnchorPx[1] = mpy - s.y;
                imp.spriteMode = SceneEditor::TextureImportSettings::SpriteMode::Multiple;
            } else {
                m_manualDrag      = ManualDrag::Create;
                m_manualAnchorPx[0] = mpx;
                m_manualAnchorPx[1] = mpy;
                m_manualCurPx[0]   = mpx;
                m_manualCurPx[1]   = mpy;
                m_spriteEditorSel = -1;
            }
        }

        if (down && m_manualDrag == ManualDrag::Create) {
            m_manualCurPx[0] = mpx;
            m_manualCurPx[1] = mpy;
        } else if (down && m_manualDrag == ManualDrag::Move &&
                   m_spriteEditorSel >= 0 &&
                   m_spriteEditorSel <
                       int(imp.spriteSheet.sprites.size())) {
            auto& s = imp.spriteSheet.sprites[size_t(m_spriteEditorSel)];
            const int newX = std::clamp(mpx - m_manualAnchorPx[0],
                                        0, texW - s.w);
            const int newY = std::clamp(mpy - m_manualAnchorPx[1],
                                        0, texH - s.h);
            if (newX != s.x || newY != s.y) {
                s.x = newX;
                s.y = newY;
                changed = true;
            }
        }

        if (rel) {
            if (m_manualDrag == ManualDrag::Create) {
                const int x0 = std::min(m_manualAnchorPx[0], m_manualCurPx[0]);
                const int y0 = std::min(m_manualAnchorPx[1], m_manualCurPx[1]);
                const int x1 = std::max(m_manualAnchorPx[0], m_manualCurPx[0]);
                const int y1 = std::max(m_manualAnchorPx[1], m_manualCurPx[1]);
                if (x1 - x0 >= 2 && y1 - y0 >= 2) {
                    SceneEditor::SubSprite s;
                    s.x = x0; s.y = y0;
                    s.w = x1 - x0;
                    s.h = y1 - y0;
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "%s_%zu",
                                  stem.empty() ? "sprite" : stem.c_str(),
                                  imp.spriteSheet.sprites.size());
                    s.name = buf;
                    imp.spriteSheet.sprites.push_back(std::move(s));
                    imp.spriteMode = SceneEditor::TextureImportSettings::SpriteMode::Multiple;
                    m_spriteEditorSel = int(imp.spriteSheet.sprites.size()) - 1;
                    changed = true;
                }
            }
            m_manualDrag = ManualDrag::None;
        }
        return changed;
    }

    void DrawSortingLayersModal() {
        if (m_openLayersModal) {
            ImGui::OpenPopup("Sorting Layers");
            m_openLayersModal = false;
        }
        if (!ImGui::BeginPopupModal("Sorting Layers", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextUnformatted("Order is back-to-front: the top entry "
                               "draws first, the bottom one draws last.");
        ImGui::Separator();

        std::vector<std::string> layers = m_project.SortingLayers();
        bool changed = false;
        int  removeAt = -1;
        int  moveUp   = -1;
        int  moveDown = -1;
        for (int i = 0; i < int(layers.size()); ++i) {
            ImGui::PushID(i);
            // Editable name field for each row.
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", layers[i].c_str());
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("##name", buf, sizeof(buf))) {
                layers[i] = buf;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Up") && i > 0) moveUp = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") &&
                i + 1 < int(layers.size())) moveDown = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove") &&
                layers.size() > 1) removeAt = i;
            ImGui::PopID();
        }
        if (moveUp >= 0) {
            std::swap(layers[size_t(moveUp)], layers[size_t(moveUp - 1)]);
            changed = true;
        }
        if (moveDown >= 0) {
            std::swap(layers[size_t(moveDown)], layers[size_t(moveDown + 1)]);
            changed = true;
        }
        if (removeAt >= 0) {
            layers.erase(layers.begin() + removeAt);
            changed = true;
        }
        ImGui::Separator();
        if (ImGui::Button("Add Layer")) {
            layers.push_back("NewLayer");
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Restore Defaults")) {
            layers   = SceneEditor::Project::DefaultSortingLayers();
            changed  = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();

        if (changed) {
            m_project.SetSortingLayers(std::move(layers));
            m_project.Save();
        }
        ImGui::EndPopup();
    }

    // Collision layer + NxN collide-matrix editor. Layers are a
    // separate concept from Sorting Layers (this is for physics
    // filtering, that's for draw order). The matrix is rendered
    // as a symmetric grid of checkboxes; tick = pair collides,
    // unticked = pair passes through.
    void DrawCollisionMatrixModal() {
        if (m_openCollisionMatrixModal) {
            ImGui::OpenPopup("Collision Matrix");
            m_openCollisionMatrixModal = false;
        }
        if (!ImGui::BeginPopupModal("Collision Matrix", nullptr,
                                     ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextUnformatted("Collider pairs whose two layers are "
                               "un-ticked don't generate overlap events\n"
                               "or push-out at runtime.");
        ImGui::Separator();
        std::vector<std::string> layers = m_project.CollisionLayers();
        bool layersChanged = false;
        int removeAt = -1;
        for (int i = 0; i < int(layers.size()); ++i) {
            ImGui::PushID(i);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", layers[i].c_str());
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputText("##cname", buf, sizeof(buf))) {
                layers[i] = buf;
                layersChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove") && layers.size() > 1) {
                removeAt = i;
            }
            ImGui::PopID();
        }
        if (removeAt >= 0) {
            layers.erase(layers.begin() + removeAt);
            layersChanged = true;
        }
        if (ImGui::Button("Add Layer")) {
            layers.push_back("NewLayer");
            layersChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Restore Defaults")) {
            layers = SceneEditor::Project::DefaultCollisionLayers();
            layersChanged = true;
        }
        if (layersChanged) {
            m_project.SetCollisionLayers(std::move(layers));
            m_project.Save();
            layers = m_project.CollisionLayers();
        }
        ImGui::Separator();
        // Symmetric NxN matrix. Column headers run vertically;
        // each cell is a checkbox keyed by the row-column pair.
        if (ImGui::BeginTable("##cmatrix", int(layers.size()) + 1,
                              ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("");
            for (const std::string& col : layers) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(col.c_str());
            }
            for (int i = 0; i < int(layers.size()); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(layers[i].c_str());
                for (int j = 0; j < int(layers.size()); ++j) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(i * 256 + j);
                    bool on = m_project.CollisionLayersCollide(
                        layers[i], layers[j]);
                    if (ImGui::Checkbox("##c", &on)) {
                        m_project.SetCollisionLayersCollide(
                            layers[i], layers[j], on);
                        m_project.Save();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // M8c12 Audio Mixer. Three sliders driving the submix bus
    // volumes Audio.cpp keeps. Changes propagate to the live engine
    // (so the user hears the new balance immediately) AND to
    // project.json (so re-opening the project restores it).
    void DrawAudioMixerModal() {
        if (m_openAudioMixerModal) {
            ImGui::OpenPopup("Audio Mixer");
            m_openAudioMixerModal = false;
        }
        if (!ImGui::BeginPopupModal("Audio Mixer", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextUnformatted(
            "Per-bus volumes apply multiplicatively on top of every\n"
            "voice's own volume. Master scales the final mix.");
        ImGui::Separator();

        // Three Master/SFX/Music sliders feed Audio::SetBusVolume;
        // the 4th Reverb slider takes a separate code path
        // (Audio::SetReverbWet + Project::SetAudioReverbWet)
        // because reverb isn't a Bus enum entry. Kept as a
        // single per-row switch so the layout stays uniform.
        enum class MixerRowKind { Master, Sfx, Music, Reverb };
        struct Row {
            const char*  label;
            MixerRowKind kind;
            float        value;
        };
        Row rows[4] = {
            { "Master",      MixerRowKind::Master,
              m_project.AudioMasterVolume() },
            { "SFX",         MixerRowKind::Sfx,
              m_project.AudioSfxVolume()    },
            { "Music",       MixerRowKind::Music,
              m_project.AudioMusicVolume()  },
            { "Reverb (Wet)", MixerRowKind::Reverb,
              m_project.AudioReverbWet()    },
        };

        bool changed = false;
        for (Row& r : rows) {
            ImGui::PushID(r.label);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::SliderFloat(r.label, &r.value, 0.0f, 1.0f,
                                   "%.2f")) {
                switch (r.kind) {
                case MixerRowKind::Master:
                    SceneEditor::Audio::SetBusVolume(
                        SceneEditor::Audio::Bus::Master, r.value);
                    m_project.SetAudioMasterVolume(r.value);
                    break;
                case MixerRowKind::Sfx:
                    SceneEditor::Audio::SetBusVolume(
                        SceneEditor::Audio::Bus::Sfx, r.value);
                    m_project.SetAudioSfxVolume(r.value);
                    break;
                case MixerRowKind::Music:
                    SceneEditor::Audio::SetBusVolume(
                        SceneEditor::Audio::Bus::Music, r.value);
                    m_project.SetAudioMusicVolume(r.value);
                    break;
                case MixerRowKind::Reverb:
                    SceneEditor::Audio::SetReverbWet(r.value);
                    m_project.SetAudioReverbWet(r.value);
                    break;
                }
                changed = true;
            }
            ImGui::PopID();
        }
        if (changed) m_project.Save();

        ImGui::Separator();
        if (ImGui::Button("Reset All")) {
            SceneEditor::Audio::SetBusVolume(
                SceneEditor::Audio::Bus::Master, 1.0f);
            SceneEditor::Audio::SetBusVolume(
                SceneEditor::Audio::Bus::Sfx, 1.0f);
            SceneEditor::Audio::SetBusVolume(
                SceneEditor::Audio::Bus::Music, 1.0f);
            SceneEditor::Audio::SetReverbWet(0.0f);
            m_project.SetAudioMasterVolume(1.0f);
            m_project.SetAudioSfxVolume(1.0f);
            m_project.SetAudioMusicVolume(1.0f);
            m_project.SetAudioReverbWet(0.0f);
            m_project.Save();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Window > Editor Preferences. Currently UI scale + theme.
    // Changes apply live via m_pendingFontRebuild / m_pendingTheme
    // Refresh; Save persists them to editor.prefs.json so the next
    // launch matches. More editor-wide toggles (autosave interval,
    // font file, etc.) can land in the same modal without touching
    // menu wiring.
    void DrawEditorPreferencesModal() {
        if (m_openEditorPrefsModal) {
            ImGui::OpenPopup("Editor Preferences");
            m_openEditorPrefsModal = false;
        }
        if (!ImGui::BeginPopupModal("Editor Preferences", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        // --- Theme ---
        ImGui::TextUnformatted("Theme");
        int themeIdx = (m_theme == EditorTheme::Light) ? 1 : 0;
        if (ImGui::RadioButton("Dark",  themeIdx == 0)) {
            m_theme = EditorTheme::Dark;
            m_pendingThemeRefresh = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Light", themeIdx == 1)) {
            m_theme = EditorTheme::Light;
            m_pendingThemeRefresh = true;
        }

        ImGui::Separator();

        // --- UI Scale ---
        ImGui::TextUnformatted(
            "UI scale multiplies font size and every ImGui padding /\n"
            "spacing constant. Applies live -- no restart needed.");

        float scale = m_uiScale;
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::SliderFloat("UI Scale", &scale, 0.75f, 3.0f,
                               "%.2fx")) {
            m_uiScale = scale;
            m_pendingFontRebuild = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset to DPI")) {
            m_uiScale = DetectUiScale(m_hwnd);
            m_pendingFontRebuild = true;
        }
        ImGui::TextDisabled("System DPI: %.2fx",
                            DetectUiScale(m_hwnd));

        ImGui::Separator();
        if (ImGui::Button("Save & Close")) {
            EditorPrefs p;
            p.uiScale = m_uiScale;
            p.theme   = m_theme;
            SaveEditorPrefs(p);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close (don't save)")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // M10c1 in-game settings overlay. Renders in BOTH editor and
    // standalone modes -- a Lua script that calls settings.show()
    // (typical pattern: bind to ESC) gets the same panel either way.
    // Two sections: audio bus sliders (re-using the Audio mixer
    // wiring) + an input rebind list driven by ScriptHost's action
    // binding map. The window is a plain ImGui::Begin (not a popup)
    // so it stacks cleanly on top of the standalone borderless
    // viewport where modal popup state would fight the lack of a
    // host dockspace.
    void DrawSettingsPanel() {
        if (!m_scriptHost.IsSettingsVisible()) {
            // Drop a stale rebind capture when the panel hides --
            // otherwise the next show would pick up an old action.
            m_settingsRebindAction.clear();
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        const float w = std::min(540.0f, io.DisplaySize.x * 0.7f);
        const float h = std::min(480.0f, io.DisplaySize.y * 0.8f);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(
            ImVec2((io.DisplaySize.x - w) * 0.5f,
                   (io.DisplaySize.y - h) * 0.5f),
            ImGuiCond_FirstUseEver);

        bool open = true;
        if (!ImGui::Begin("Settings##m10c1", &open,
                          ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            if (!open) m_scriptHost.SetSettingsVisible(false);
            return;
        }
        if (!open) {
            m_scriptHost.SetSettingsVisible(false);
            ImGui::End();
            return;
        }

        // --- Audio section ---------------------------------------
        if (ImGui::CollapsingHeader("Audio",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            struct Row {
                const char* label;
                float       value;
                int         kind; // 0=master, 1=sfx, 2=music, 3=reverb
            };
            Row rows[4] = {
                {"Master",      m_project.AudioMasterVolume(), 0},
                {"SFX",         m_project.AudioSfxVolume(),    1},
                {"Music",       m_project.AudioMusicVolume(),  2},
                {"Reverb (Wet)",m_project.AudioReverbWet(),    3},
            };
            bool changed = false;
            for (Row& r : rows) {
                ImGui::PushID(r.label);
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::SliderFloat(r.label, &r.value,
                                       0.0f, 1.0f, "%.2f")) {
                    switch (r.kind) {
                    case 0:
                        SceneEditor::Audio::SetBusVolume(
                            SceneEditor::Audio::Bus::Master, r.value);
                        m_project.SetAudioMasterVolume(r.value);
                        break;
                    case 1:
                        SceneEditor::Audio::SetBusVolume(
                            SceneEditor::Audio::Bus::Sfx, r.value);
                        m_project.SetAudioSfxVolume(r.value);
                        break;
                    case 2:
                        SceneEditor::Audio::SetBusVolume(
                            SceneEditor::Audio::Bus::Music, r.value);
                        m_project.SetAudioMusicVolume(r.value);
                        break;
                    case 3:
                        SceneEditor::Audio::SetReverbWet(r.value);
                        m_project.SetAudioReverbWet(r.value);
                        break;
                    }
                    changed = true;
                }
                ImGui::PopID();
            }
            // In editor mode we persist into project.json so the
            // mixer slider matches next time. Standalone uses the
            // same UI but skips the project write (the runtime
            // copy of project.json lives next to the .exe and we
            // don't want surprise file changes in a shipped build).
            if (changed && !m_standalone) m_project.Save();
        }

        ImGui::Separator();

        // --- Input rebind section --------------------------------
        if (ImGui::CollapsingHeader("Input",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto bindings = m_scriptHost.AllActionBindings();
            if (bindings.empty()) {
                ImGui::TextDisabled(
                    "No actions bound. Call input.bind(action, key)\n"
                    "from a script before opening this panel.");
            } else {
                if (ImGui::BeginTable("##m10c1_bindings", 3,
                        ImGuiTableFlags_SizingStretchProp |
                        ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Action",
                        ImGuiTableColumnFlags_WidthStretch, 0.50f);
                    ImGui::TableSetupColumn("Key",
                        ImGuiTableColumnFlags_WidthStretch, 0.30f);
                    ImGui::TableSetupColumn("##rebind",
                        ImGuiTableColumnFlags_WidthStretch, 0.20f);
                    for (const auto& kv : bindings) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(kv.first.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(kv.second.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::PushID(kv.first.c_str());
                        const bool capturing =
                            m_settingsRebindAction == kv.first;
                        const char* btn = capturing
                            ? "[press a key]" : "Rebind";
                        if (ImGui::Button(btn, ImVec2(-FLT_MIN, 0))) {
                            m_settingsRebindAction =
                                capturing ? std::string{} : kv.first;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            if (!m_settingsRebindAction.empty()) {
                if (const char* captured = CaptureRebindKey()) {
                    m_scriptHost.SetActionBinding(
                        m_settingsRebindAction, captured);
                    m_settingsRebindAction.clear();
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            m_scriptHost.SetSettingsVisible(false);
        }
        ImGui::End();
    }

    // M10c5 in-game dialog box. Same dual-mode pattern as the
    // settings overlay: ScriptHost owns the (visible / text /
    // choices / last_choice) state; this panel renders an
    // ImGui::Begin window when visible and writes the picked
    // index back via SetDialogResult on click. Lua's
    // `dialog.show("hi")` shows a single OK button (index 0);
    // `dialog.choices(prompt, {"a","b","c"})` shows three
    // buttons. Buttons render in a horizontal row to keep
    // typical "Yes / No / Cancel" dialogs visually compact;
    // long lists wrap automatically when they overflow.
    void DrawDialogPanel() {
        if (!m_scriptHost.IsDialogVisible()) return;
        ImGuiIO& io = ImGui::GetIO();
        const float w = std::min(560.0f, io.DisplaySize.x * 0.7f);
        const float h = std::min(220.0f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        // Anchor near the bottom-centre in standalone (game-feel
        // dialog-box position) + dead-centre in editor mode
        // (clearer that it's a transient prompt).
        const float anchorY = m_standalone
            ? (io.DisplaySize.y - h - io.DisplaySize.y * 0.10f)
            : (io.DisplaySize.y - h) * 0.5f;
        ImGui::SetNextWindowPos(
            ImVec2((io.DisplaySize.x - w) * 0.5f,
                   std::max(0.0f, anchorY)),
            ImGuiCond_Always);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoScrollbar;
        ImGui::Begin("##m10c5_dialog", nullptr, flags);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(m_scriptHost.DialogText().c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const auto& choices = m_scriptHost.DialogChoices();
        if (choices.empty()) {
            // Single OK button -- centred. Index 0 in C++,
            // becomes 1 in Lua's last_choice() (1-based).
            const float btnW = 100.0f;
            ImGui::SetCursorPosX(
                (ImGui::GetContentRegionAvail().x - btnW) * 0.5f);
            if (ImGui::Button("OK", ImVec2(btnW, 0))) {
                m_scriptHost.SetDialogResult(0);
            }
        } else {
            const float pad   = ImGui::GetStyle().ItemSpacing.x;
            const float avail = ImGui::GetContentRegionAvail().x;
            float lineW = 0.0f;
            for (size_t i = 0; i < choices.size(); ++i) {
                const std::string label = choices[i].empty()
                    ? std::string("(unnamed)") : choices[i];
                const float textW =
                    ImGui::CalcTextSize(label.c_str()).x +
                    ImGui::GetStyle().FramePadding.x * 2.0f;
                const float btnW = std::max(80.0f, textW + 16.0f);
                if (i > 0) {
                    if (lineW + pad + btnW > avail) {
                        // Wrap to next row
                        lineW = 0.0f;
                    } else {
                        ImGui::SameLine();
                    }
                }
                ImGui::PushID(int(i));
                if (ImGui::Button(label.c_str(), ImVec2(btnW, 0))) {
                    m_scriptHost.SetDialogResult(int(i));
                }
                ImGui::PopID();
                lineW = (lineW > 0.0f ? lineW + pad : 0.0f) + btnW;
            }
        }
        ImGui::End();
    }

    // Captures the first non-modifier key currently pressed and
    // maps it back to the string spellings ScriptHost::ActionKey
    // understands (single letter A-Z / 0-9, named keys like Space
    // / Enter / Up). Returns nullptr when nothing usable is down,
    // so the caller polls each frame until something lands.
    const char* CaptureRebindKey() {
        struct Entry { ImGuiKey key; const char* name; };
        static const Entry kTable[] = {
            {ImGuiKey_Space,     "Space"},
            {ImGuiKey_Enter,     "Enter"},
            {ImGuiKey_Escape,    "Escape"},
            {ImGuiKey_Tab,       "Tab"},
            {ImGuiKey_Backspace, "Backspace"},
            {ImGuiKey_Delete,    "Delete"},
            {ImGuiKey_Home,      "Home"},
            {ImGuiKey_End,       "End"},
            {ImGuiKey_PageUp,    "PageUp"},
            {ImGuiKey_PageDown,  "PageDown"},
            {ImGuiKey_UpArrow,   "Up"},
            {ImGuiKey_DownArrow, "Down"},
            {ImGuiKey_LeftArrow, "Left"},
            {ImGuiKey_RightArrow,"Right"},
            {ImGuiKey_LeftShift, "LeftShift"},
            {ImGuiKey_RightShift,"RightShift"},
            {ImGuiKey_LeftCtrl,  "LeftCtrl"},
            {ImGuiKey_RightCtrl, "RightCtrl"},
            {ImGuiKey_LeftAlt,   "LeftAlt"},
            {ImGuiKey_RightAlt,  "RightAlt"},
        };
        // Single A-Z and 0-9 are emitted as static character buffers
        // so the caller (which copies into std::string) doesn't have
        // to manage the lifetime.
        static thread_local char single[2] = {0, 0};
        for (int i = 0; i < 26; ++i) {
            if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_A + i), false)) {
                single[0] = char('A' + i);
                return single;
            }
        }
        for (int i = 0; i < 10; ++i) {
            if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_0 + i), false)) {
                single[0] = char('0' + i);
                return single;
            }
        }
        for (const Entry& e : kTable) {
            if (ImGui::IsKeyPressed(e.key, false)) return e.name;
        }
        return nullptr;
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
            // See m_pendingBrowseForNewProjectPath comment: firing
            // IFileOpenDialog from inside BeginPopupModal freezes
            // the app; defer to OnRender's tail instead.
            m_pendingBrowseForNewProjectPath = true;
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
                ApplyProjectAudioMixer();
                ReloadAllScripts();
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
            UndoSnapshot();
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

    // M11c5 recursive entity duplication. Mints a fresh entity for
    // each node in the source subtree, copies every component
    // through a Serialize -> Deserialize round-trip (the same path
    // scene save/load uses, so behaviour mirrors what a save +
    // reload would produce), + recurses into children so a copied
    // node also carries its descendants.
    //
    // Naming: the root copy gets a " (Copy)" suffix to keep the
    // Hierarchy panel unambiguous; children keep their original
    // names so a "Wheel" inside "Car (Copy)" reads correctly.
    SceneEditor::EntityId DuplicateEntityRecursive(
            SceneEditor::EntityId srcId,
            SceneEditor::EntityId newParent,
            bool topLevel = true) {
        SceneEditor::Entity* src = m_scene.Get(srcId);
        if (!src) return SceneEditor::kInvalidEntityId;
        const std::string baseName = topLevel
            ? src->name + " (Copy)" : src->name;
        // Snapshot the source's component data + child list before
        // we touch the scene -- CreateEntity may grow the entity
        // map, and any later Get() calls on srcId return a fresh
        // pointer to the same unique_ptr, but we re-fetch to be
        // safe.
        std::vector<std::pair<std::string, NaraGame::MiniJson::Value>>
            compData;
        compData.reserve(src->components.size());
        for (const auto& srcC : src->components) {
            if (!srcC) continue;
            NaraGame::MiniJson::Value tmp =
                NaraGame::MiniJson::Value::MakeObject();
            srcC->Serialize(tmp);
            compData.emplace_back(srcC->TypeName(), std::move(tmp));
        }
        const std::vector<SceneEditor::EntityId> srcChildren = src->children;
        const bool srcEnabled = src->enabled;
        const SceneEditor::EntityId copy =
            m_scene.CreateEntity(baseName, newParent);
        SceneEditor::Entity* dst = m_scene.Get(copy);
        if (!dst) return SceneEditor::kInvalidEntityId;
        dst->enabled = srcEnabled;
        // Drop the default Transform CreateEntity installed -- the
        // source's serialised Transform comes in via the loop
        // below.
        dst->components.clear();
        for (auto& [typeName, payload] : compData) {
            auto fresh =
                SceneEditor::ComponentRegistry::Get().Create(typeName);
            if (!fresh) continue;
            // LuaComponent needs its dynamic typeName re-applied
            // before Deserialize (the fallback factory used the
            // typeName but the payload's "type" key is what the
            // scene loader's outer envelope owns -- here we copy
            // it onto the fresh instance directly).
            if (auto* lc =
                    dynamic_cast<SceneEditor::LuaComponent*>(fresh.get())) {
                lc->SetTypeName(typeName);
            }
            fresh->Deserialize(payload);
            dst->components.push_back(std::move(fresh));
        }
        // Recurse children. Each child becomes a child of the new
        // copy (not the original parent).
        for (SceneEditor::EntityId childId : srcChildren) {
            DuplicateEntityRecursive(childId, copy, /*topLevel*/ false);
        }
        return copy;
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
                UndoSnapshot();
                m_scene.CreateEntity("Entity", id);
                UpdateWindowTitle();
            }
            if (ImGui::MenuItem("Duplicate")) {
                UndoSnapshot();
                const SceneEditor::EntityId copy =
                    DuplicateEntityRecursive(id, e->parent);
                if (copy != SceneEditor::kInvalidEntityId) {
                    m_selection = copy;
                }
                UpdateWindowTitle();
            }
            if (ImGui::MenuItem("Delete")) {
                UndoSnapshot();
                NotifyScriptDestroyForEntity(id);
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
        if (ImGui::IsItemActivated()) UndoSnapshot();
        {
            bool draft = e->enabled;
            if (ImGui::Checkbox("Enabled", &draft)) {
                UndoSnapshot();
                e->enabled = draft;
                m_scene.MarkDirty();
                UpdateWindowTitle();
            }
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
                        UndoSnapshot();
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
        } else if (auto* tm = dynamic_cast<SceneEditor::TilemapComponent*>(comp)) {
            DrawTilemapEditor(entity, *tm);
        } else if (auto* an = dynamic_cast<SceneEditor::AnimatorComponent*>(comp)) {
            DrawAnimatorEditor(entity, *an);
        } else if (auto* sc = dynamic_cast<SceneEditor::ScriptComponent*>(comp)) {
            DrawScriptEditor(entity, *sc);
        } else if (auto* as = dynamic_cast<SceneEditor::AudioSourceComponent*>(comp)) {
            DrawAudioSourceEditor(entity, *as);
        } else if (auto* al = dynamic_cast<SceneEditor::AudioListenerComponent*>(comp)) {
            DrawAudioListenerEditor(entity, *al);
        } else if (auto* bc = dynamic_cast<SceneEditor::BoxColliderComponent*>(comp)) {
            DrawBoxColliderEditor(entity, *bc);
        } else if (auto* cc = dynamic_cast<SceneEditor::CircleColliderComponent*>(comp)) {
            DrawCircleColliderEditor(entity, *cc);
        } else if (auto* cap = dynamic_cast<SceneEditor::CapsuleColliderComponent*>(comp)) {
            DrawCapsuleColliderEditor(entity, *cap);
        } else if (auto* pgc = dynamic_cast<SceneEditor::PolygonColliderComponent*>(comp)) {
            DrawPolygonColliderEditor(entity, *pgc);
        } else if (auto* ec = dynamic_cast<SceneEditor::EdgeColliderComponent*>(comp)) {
            DrawEdgeColliderEditor(entity, *ec);
        } else if (auto* rb = dynamic_cast<SceneEditor::Rigidbody2DComponent*>(comp)) {
            DrawRigidbody2DEditor(entity, *rb);
        } else if (auto* ps = dynamic_cast<SceneEditor::ParticleSystemComponent*>(comp)) {
            DrawParticleSystemEditor(entity, *ps);
        } else if (auto* cm = dynamic_cast<SceneEditor::CameraComponent*>(comp)) {
            DrawCameraEditor(entity, *cm);
        } else if (auto* sp = dynamic_cast<SceneEditor::SplashScreenComponent*>(comp)) {
            DrawSplashScreenEditor(entity, *sp);
        } else if (auto* lc = dynamic_cast<SceneEditor::LuaComponent*>(comp)) {
            DrawLuaComponentEditor(entity, *lc);
        } else {
            ImGui::TextDisabled("(no inspector yet for %s)", comp->TypeName());
        }
    }

    // M11c3 generic inspector for Lua-defined components. Top-level
    // string / number / bool fields get inline editors; nested
    // objects + arrays render as JSON-style read-only previews
    // (scripts mutate those via component.get / component.set, not
    // the editor). Edits write straight back to the field table,
    // so the next scene save persists them.
    void DrawLuaComponentEditor(SceneEditor::Entity& /*entity*/,
                                 SceneEditor::LuaComponent& lc) {
        ImGui::TextDisabled("Custom type: %s", lc.Type().c_str());
        auto& fields = lc.Fields();
        if (!fields.IsObject() || fields.objectKeys.empty()) {
            ImGui::TextDisabled("(no fields)");
            return;
        }
        for (size_t i = 0; i < fields.objectKeys.size(); ++i) {
            const std::string& key = fields.objectKeys[i];
            NaraGame::MiniJson::Value& v = fields.objectValues[i];
            ImGui::PushID(int(i));
            if (v.IsNumber()) {
                float fv = float(v.numberValue);
                if (ImGui::DragFloat(key.c_str(), &fv, 0.1f)) {
                    UndoSnapshot();
                    v.numberValue = double(fv);
                    m_scene.MarkDirty();
                }
            } else if (v.IsBool()) {
                bool bv = v.boolValue;
                if (ImGui::Checkbox(key.c_str(), &bv)) {
                    UndoSnapshot();
                    v.boolValue = bv;
                    m_scene.MarkDirty();
                }
            } else if (v.IsString()) {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s",
                               v.stringValue.c_str());
                if (ImGui::InputText(key.c_str(), buf, sizeof(buf))) {
                    UndoSnapshot();
                    v.stringValue = buf;
                    m_scene.MarkDirty();
                }
            } else {
                // Object / array / null -- show a compact label so
                // the user knows the field exists. Editing is
                // script-side only for these.
                const char* tag = v.IsObject() ? "{object}"
                                : v.IsArray()  ? "[array]"
                                                : "(null)";
                ImGui::Text("%s = %s", key.c_str(), tag);
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        if (ImGui::SmallButton("Remove##luacomp")) {
            UndoSnapshot();
            // Erase by raw pointer comparison like the other inspectors.
            for (auto it = m_scene.Get(m_selection)->components.begin();
                 it != m_scene.Get(m_selection)->components.end(); ++it) {
                if (it->get() == &lc) {
                    m_scene.Get(m_selection)->components.erase(it);
                    break;
                }
            }
            m_scene.MarkDirty();
        }
    }

    // M10c2 splash screen inspector. Same texture-picker pattern as
    // SpriteRenderer + simple float fields for the timeline. The
    // `nextScene` field is a plain text input (scene paths follow
    // the same convention as Lua's scene.load) so the inspector
    // doesn't need a scene picker dialog; a typo just errors out
    // on the asset-not-found at load time.
    void DrawSplashScreenEditor(SceneEditor::Entity& /*entity*/,
                                 SceneEditor::SplashScreenComponent& sp) {
        // Texture drop-zone -- same idiom as the SpriteRenderer
        // inspector. Shows the relative path or a placeholder
        // hint + accepts an ASSET_GUID drag-drop payload.
        std::string label = "(none)";
        if (sp.texture.IsValid()) {
            if (const auto* entry = m_assetDb.FindByGuid(sp.texture)) {
                label = entry->relPath;
            } else {
                label = "(missing asset)";
            }
        }
        ImGui::TextUnformatted("Texture");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::Button("Drop texture here##splash_tex", ImVec2(-FLT_MIN, 0));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                if (p->DataSize == sizeof(SceneEditor::AssetGuid)) {
                    UndoSnapshot();
                    std::memcpy(&sp.texture, p->Data, sizeof(sp.texture));
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (sp.texture.IsValid()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##splash_tex")) {
                UndoSnapshot();
                sp.texture = SceneEditor::AssetGuid{};
            }
        }

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", sp.nextScene.c_str());
        if (ImGui::InputText("Next scene", buf, sizeof(buf))) {
            UndoSnapshot();
            sp.nextScene = buf;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Path or GUID handed to scene.load when the\n"
                "splash timeline completes. Empty = stay here.");
        }
        if (ImGui::DragFloat("Fade in",  &sp.fadeIn,  0.01f,
                              0.0f, 30.0f, "%.2f s"))  { UndoSnapshot(); }
        if (ImGui::DragFloat("Hold",     &sp.hold,    0.01f,
                              0.0f, 30.0f, "%.2f s"))  { UndoSnapshot(); }
        if (ImGui::DragFloat("Fade out", &sp.fadeOut, 0.01f,
                              0.0f, 30.0f, "%.2f s"))  { UndoSnapshot(); }
        if (ImGui::DragInt   ("Priority", &sp.priority,
                              1.0f, -1024, 1024))      { UndoSnapshot(); }
        ImGui::ColorEdit4("Background", &sp.background.r);
        ImGui::ColorEdit4("Tint",       &sp.tint.r);
        ImGui::TextDisabled("Total: %.2f s", sp.TotalDuration());
    }

    // M9c11 Camera inspector. Two fields drive everything:
    // `zoom` (pixels per world unit) + `priority` (higher
    // wins when multiple cameras coexist). The active-camera
    // resolver re-runs every standalone frame so changes are
    // immediate.
    void DrawCameraEditor(SceneEditor::Entity& /*entity*/,
                           SceneEditor::CameraComponent& cm) {
        if (ImGui::DragFloat("Zoom (px/unit)", &cm.zoom,
                              0.5f, 1.0f, 1024.0f, "%.1f")) {
            UndoSnapshot();
            if (cm.zoom < 0.01f) cm.zoom = 0.01f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Pixels per world unit. Matches the editor's\n"
                "viewport-camera scale; standalone runs use\n"
                "this directly. Higher = closer / bigger\n"
                "objects on screen.");
        }
        if (ImGui::DragInt("Priority", &cm.priority,
                            1.0f, -1024, 1024)) {
            UndoSnapshot();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "When multiple enabled CameraComponents exist,\n"
                "the highest priority wins. Ties break in\n"
                "scene-traversal order. A cutscene camera at\n"
                "priority 10 grabs the viewport from a\n"
                "player-follow camera at priority 0 the moment\n"
                "you enable it.");
        }
        // Quick "Frame in viewport" shortcut: snap the editor
        // camera to this component so the user can preview
        // what the game will see.
        if (ImGui::Button("Preview in Viewport")) {
            if (const auto* tr = m_scene.Get(m_selection)
                    ? m_scene.Get(m_selection)
                        ->GetComponent<SceneEditor::TransformComponent>()
                    : nullptr) {
                m_viewCam.center = ImVec2{tr->position.x,
                                           tr->position.y};
                m_viewCam.zoom   = cm.zoom;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Snap the editor's pan + zoom to match this\n"
                "camera. Editor-only convenience; standalone\n"
                "always uses the highest-priority camera live.");
        }
    }

    // M9c2 Particle System inspector. The numeric ranges
    // (lifetime / speed / size / angle) get min-max pairs;
    // colour pickers do startColor / endColor with alpha so
    // the user can build a fade-out tail. Reset clears every
    // live particle + re-arms the rising-edge auto-play
    // gate, which is the easiest way to preview the new
    // settings without leaving Play mode.
    void DrawParticleSystemEditor(SceneEditor::Entity& /*entity*/,
                                   SceneEditor::ParticleSystemComponent& ps) {
        ImGui::Text("Live: %d / %d", ps._liveCount, ps.maxParticles);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            ps._liveCount    = 0;
            ps._emitAccum    = 0.0f;
            ps._emittedTotal = 0;
            ps._started      = false;
        }

        int mp = ps.maxParticles;
        if (ImGui::DragInt("Max Particles", &mp, 1.0f, 1, 4096)) {
            UndoSnapshot();
            if (mp < 1) mp = 1;
            if (mp > 4096) mp = 4096;
            ps.maxParticles = mp;
        }
        if (ImGui::DragFloat("Emission Rate", &ps.emissionRate,
                              0.5f, 0.0f, 0.0f, "%.1f /sec")) {
            UndoSnapshot();
            if (ps.emissionRate < 0.0f) ps.emissionRate = 0.0f;
        }

        float lt[2] = { ps.lifetimeMin, ps.lifetimeMax };
        if (ImGui::DragFloat2("Lifetime (min/max)", lt, 0.02f,
                               0.0f, 0.0f, "%.2f s")) {
            UndoSnapshot();
            if (lt[0] < 0.01f) lt[0] = 0.01f;
            if (lt[1] < lt[0]) lt[1] = lt[0];
            ps.lifetimeMin = lt[0]; ps.lifetimeMax = lt[1];
        }
        float sp[2] = { ps.startSpeedMin, ps.startSpeedMax };
        if (ImGui::DragFloat2("Start Speed (min/max)", sp, 0.05f,
                               0.0f, 0.0f, "%.2f")) {
            UndoSnapshot();
            if (sp[1] < sp[0]) sp[1] = sp[0];
            ps.startSpeedMin = sp[0]; ps.startSpeedMax = sp[1];
        }
        float sz[2] = { ps.startSizeMin, ps.startSizeMax };
        if (ImGui::DragFloat2("Start Size (min/max)", sz, 0.01f,
                               0.001f, 0.0f, "%.3f")) {
            UndoSnapshot();
            if (sz[0] < 0.001f) sz[0] = 0.001f;
            if (sz[1] < sz[0])  sz[1] = sz[0];
            ps.startSizeMin = sz[0]; ps.startSizeMax = sz[1];
        }
        // M9c8 end-of-life size. The -1 sentinel surfaces as
        // a "Size over Lifetime" checkbox: unchecked = both
        // ends sit at -1 (no lerp -> particle stays at its
        // start size); checked = real values flow into the
        // DragFloat2 + lerp kicks in. Switching the toggle
        // off preserves the previous values via -1, so
        // toggling back on doesn't lose what the user typed.
        bool sizeLerp = (ps.endSizeMin >= 0.0f);
        if (ImGui::Checkbox("Size over Lifetime", &sizeLerp)) {
            UndoSnapshot();
            if (sizeLerp) {
                // Default the lerp endpoints to 0 (shrink to
                // nothing) when the user enables the feature
                // for the first time -- the most common case.
                ps.endSizeMin = 0.0f;
                ps.endSizeMax = 0.0f;
            } else {
                ps.endSizeMin = -1.0f;
                ps.endSizeMax = -1.0f;
            }
        }
        if (sizeLerp) {
            float ez[2] = { ps.endSizeMin, ps.endSizeMax };
            if (ImGui::DragFloat2("End Size (min/max)", ez,
                                   0.01f, 0.0f, 0.0f, "%.3f")) {
                UndoSnapshot();
                if (ez[0] < 0.0f) ez[0] = 0.0f;
                if (ez[1] < ez[0]) ez[1] = ez[0];
                ps.endSizeMin = ez[0]; ps.endSizeMax = ez[1];
            }
        }
        float ang[2] = { ps.dirAngleMinDeg, ps.dirAngleMaxDeg };
        if (ImGui::DragFloat2("Direction (deg, min/max)", ang,
                               1.0f, -360.0f, 720.0f, "%.0f")) {
            UndoSnapshot();
            if (ang[1] < ang[0]) ang[1] = ang[0];
            ps.dirAngleMinDeg = ang[0]; ps.dirAngleMaxDeg = ang[1];
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Emission cone in degrees. 0 = +X, 90 = +Y (up),\n"
                "180 = -X. (45, 135) gives an upward fan;\n"
                "(0, 360) is omnidirectional.");
        }

        float sc[4] = { ps.startColor.r, ps.startColor.g,
                        ps.startColor.b, ps.startColor.a };
        if (ImGui::ColorEdit4("Start Color", sc)) {
            UndoSnapshot();
            ps.startColor = { sc[0], sc[1], sc[2], sc[3] };
        }
        float ec[4] = { ps.endColor.r, ps.endColor.g,
                        ps.endColor.b, ps.endColor.a };
        if (ImGui::ColorEdit4("End Color", ec)) {
            UndoSnapshot();
            ps.endColor = { ec[0], ec[1], ec[2], ec[3] };
        }

        float gv[2] = { ps.gravity.x, ps.gravity.y };
        if (ImGui::DragFloat2("Gravity", gv, 0.05f, 0.0f, 0.0f,
                               "%.2f")) {
            UndoSnapshot();
            ps.gravity = { gv[0], gv[1] };
        }

        // M9c10 emission shape + per-shape size params. Only
        // the field relevant to the picked shape shows; the
        // others stay in the component so a Point -> Circle ->
        // Point round-trip doesn't lose the radius the user set.
        using Sh = SceneEditor::ParticleEmissionShape;
        static const char* kShapeNames[] = {
            "Point", "Circle Edge", "Circle Volume",
            "Box Edge", "Box Volume", "Line"
        };
        int shapeIdx = int(ps.shape);
        if (ImGui::Combo("Shape", &shapeIdx,
                          kShapeNames, IM_ARRAYSIZE(kShapeNames))) {
            UndoSnapshot();
            ps.shape = Sh(shapeIdx);
        }
        if (ps.shape == Sh::CircleEdge ||
            ps.shape == Sh::CircleVolume) {
            if (ImGui::DragFloat("Radius", &ps.shapeRadius,
                                  0.02f, 0.0f, 0.0f, "%.2f")) {
                UndoSnapshot();
                if (ps.shapeRadius < 0.0f) ps.shapeRadius = 0.0f;
            }
        } else if (ps.shape == Sh::BoxEdge ||
                    ps.shape == Sh::BoxVolume) {
            float bs[2] = { ps.shapeBoxSize.x, ps.shapeBoxSize.y };
            if (ImGui::DragFloat2("Box Size (w/h)", bs,
                                   0.02f, 0.0f, 0.0f, "%.2f")) {
                UndoSnapshot();
                if (bs[0] < 0.0f) bs[0] = 0.0f;
                if (bs[1] < 0.0f) bs[1] = 0.0f;
                ps.shapeBoxSize = { bs[0], bs[1] };
            }
        } else if (ps.shape == Sh::Line) {
            float le[2] = { ps.shapeLineEnd.x, ps.shapeLineEnd.y };
            if (ImGui::DragFloat2("Line End (offset)", le,
                                   0.05f, 0.0f, 0.0f, "%.2f")) {
                UndoSnapshot();
                ps.shapeLineEnd = { le[0], le[1] };
            }
        }

        // M9c7 rotation block. Velocity-align dominates -- when
        // it's on the start/spin ranges are still serialised
        // (toggling the flag off snaps live particles back to
        // their stored angles instead of all to 0) but they
        // don't affect what the user sees on screen.
        float rotRange[2] = { ps.startRotationMinDeg,
                              ps.startRotationMaxDeg };
        if (ImGui::DragFloat2("Start Rotation (deg, min/max)",
                               rotRange, 1.0f, -720.0f, 720.0f,
                               "%.0f")) {
            UndoSnapshot();
            if (rotRange[1] < rotRange[0]) rotRange[1] = rotRange[0];
            ps.startRotationMinDeg = rotRange[0];
            ps.startRotationMaxDeg = rotRange[1];
        }
        float spinRange[2] = { ps.angularSpeedMinDeg,
                                ps.angularSpeedMaxDeg };
        if (ImGui::DragFloat2("Angular Speed (deg/s, min/max)",
                               spinRange, 2.0f, -3600.0f, 3600.0f,
                               "%.0f")) {
            UndoSnapshot();
            if (spinRange[1] < spinRange[0]) spinRange[1] = spinRange[0];
            ps.angularSpeedMinDeg = spinRange[0];
            ps.angularSpeedMaxDeg = spinRange[1];
        }
        if (ImGui::Checkbox("Velocity Align", &ps.velocityAlign)) {
            UndoSnapshot();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "When on, each particle's quad rotates to face\n"
                "its velocity direction (good for sparks /\n"
                "debris streaks). Overrides the start rotation\n"
                "+ angular speed for the live render path.");
        }

        // M9c4 texture picker. Same combo + drag-drop shape
        // as the SpriteRenderer's sprite field. Empty = the
        // untextured AddRectFilled path (the M9c2 default).
        std::string texLabel = "(none)";
        if (ps.texture.IsValid()) {
            if (const auto* en = m_assetDb.FindByGuid(ps.texture)) {
                texLabel = en->relPath;
            } else {
                texLabel = ps.texture.ToString();
            }
        }
        if (ImGui::BeginCombo("Texture", texLabel.c_str())) {
            if (ImGui::Selectable("(none)", !ps.texture.IsValid())) {
                UndoSnapshot();
                ps.texture = SceneEditor::AssetGuid{};
            }
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Texture") continue;
                const bool sel = (a.guid == ps.texture);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    UndoSnapshot();
                    ps.texture = a.guid;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                if (p->DataSize == sizeof(SceneEditor::AssetGuid)) {
                    SceneEditor::AssetGuid guid;
                    std::memcpy(&guid, p->Data, sizeof(guid));
                    if (const auto* en = m_assetDb.FindByGuid(guid)) {
                        if (en->typeName == "Texture") {
                            UndoSnapshot();
                            ps.texture = guid;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // M9c5 sub-sprite picker. Only meaningful when the
        // bound texture has a Multiple-mode sprite sheet --
        // mirrors the SpriteRenderer pattern, including the
        // "(whole texture)" default + orphan pin so a stale
        // rename keeps editing instead of going invisible.
        if (ps.texture.IsValid()) {
            if (const auto* entry = m_assetDb.FindByGuid(ps.texture)) {
                SceneEditor::TextureImportSettings imp;
                imp.FromJson(entry->importSettings);
                const auto& subs = imp.spriteSheet.sprites;
                if (!subs.empty()) {
                    bool nameInList = ps.subSprite.empty();
                    for (const auto& sub : subs) {
                        if (sub.name == ps.subSprite) {
                            nameInList = true; break;
                        }
                    }
                    const char* preview = ps.subSprite.empty()
                        ? "(whole texture)" : ps.subSprite.c_str();
                    if (ImGui::BeginCombo("Sub-sprite", preview)) {
                        if (!nameInList && !ps.subSprite.empty()) {
                            if (ImGui::Selectable(
                                    (ps.subSprite + "  (orphan)").c_str(),
                                    true)) {}
                            ImGui::Separator();
                        }
                        if (ImGui::Selectable("(whole texture)",
                                              ps.subSprite.empty())) {
                            UndoSnapshot();
                            ps.subSprite.clear();
                        }
                        for (const auto& sub : subs) {
                            const bool sel = (sub.name == ps.subSprite);
                            if (ImGui::Selectable(sub.name.c_str(), sel)) {
                                UndoSnapshot();
                                ps.subSprite = sub.name;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
        }

        // M9c6 per-particle frame animation. List of sub-sprite
        // names + per-second rate + loop toggle. The
        // sub-sprite combo above keeps editing the M9c5 single-
        // cell path; once one or more frames are added here
        // the renderer switches to the animated path + ignores
        // `subSprite` until the list is cleared.
        if (ps.texture.IsValid()) {
            std::vector<SceneEditor::SubSprite> subs;
            if (const auto* entry = m_assetDb.FindByGuid(ps.texture)) {
                SceneEditor::TextureImportSettings imp;
                imp.FromJson(entry->importSettings);
                subs = imp.spriteSheet.sprites;
            }
            ImGui::Text("Frames: %d", int(ps.animFrames.size()));
            ImGui::SameLine();
            if (ImGui::SmallButton("Add##frame")) {
                UndoSnapshot();
                ps.animFrames.emplace_back();
            }
            if (!ps.animFrames.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##frames")) {
                    UndoSnapshot();
                    ps.animFrames.clear();
                }
            }
            int removeAt = -1;
            for (int i = 0; i < int(ps.animFrames.size()); ++i) {
                ImGui::PushID(i);
                std::string& frame = ps.animFrames[size_t(i)];
                const char* preview =
                    frame.empty() ? "(none)" : frame.c_str();
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::BeginCombo("##framepick", preview)) {
                    if (ImGui::Selectable("(none)", frame.empty())) {
                        UndoSnapshot();
                        frame.clear();
                    }
                    for (const auto& sub : subs) {
                        const bool sel = (sub.name == frame);
                        if (ImGui::Selectable(sub.name.c_str(), sel)) {
                            UndoSnapshot();
                            frame = sub.name;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) removeAt = i;
                ImGui::PopID();
            }
            if (removeAt >= 0) {
                UndoSnapshot();
                ps.animFrames.erase(
                    ps.animFrames.begin() + removeAt);
            }
            if (!ps.animFrames.empty()) {
                if (ImGui::DragFloat("Anim FPS", &ps.animFps,
                                      0.5f, 0.0f, 0.0f, "%.1f")) {
                    UndoSnapshot();
                    if (ps.animFps < 0.0f) ps.animFps = 0.0f;
                }
                if (ImGui::Checkbox("Anim Loop", &ps.animLoop)) {
                    UndoSnapshot();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Loop: wrap via modulo for fire / sparkle\n"
                        "loops. Off: clamp at the last frame --\n"
                        "use for one-shot explosion sequences.");
                }
            }
        }

        if (ImGui::Checkbox("Play On Start", &ps.playOnStart)) {
            UndoSnapshot();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Loop", &ps.loop)) UndoSnapshot();

        // M9c9 burst events. duration sets the cycle length;
        // when Loop is on the emitter wraps at duration + re-
        // fires the bursts. Order in the list is the order
        // they fire -- sort by time for predictable results.
        if (ImGui::DragFloat("Duration (s)", &ps.duration,
                              0.05f, 0.0f, 0.0f, "%.2f")) {
            UndoSnapshot();
            if (ps.duration < 0.0f) ps.duration = 0.0f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Cycle length in seconds. When Loop is on,\n"
                "the emitter resets its clock + the next-\n"
                "burst cursor every `duration` so bursts\n"
                "re-fire. 0 disables the cycle (bursts fire\n"
                "once at start).");
        }
        ImGui::Text("Bursts: %d", int(ps.bursts.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton("Add##burst")) {
            UndoSnapshot();
            ps.bursts.push_back(
                SceneEditor::ParticleSystemComponent::Burst{});
        }
        if (!ps.bursts.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Sort##burst")) {
                UndoSnapshot();
                std::sort(ps.bursts.begin(), ps.bursts.end(),
                    [](const auto& a, const auto& b) {
                        return a.time < b.time;
                    });
            }
        }
        int removeBurst = -1;
        for (int i = 0; i < int(ps.bursts.size()); ++i) {
            ImGui::PushID(10000 + i);
            auto& b = ps.bursts[size_t(i)];
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::DragFloat("##bt", &b.time, 0.02f,
                                  0.0f, 0.0f, "t=%.2f")) {
                UndoSnapshot();
                if (b.time < 0.0f) b.time = 0.0f;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::DragInt("##bc", &b.count, 0.5f,
                                0, 4096, "n=%d")) {
                UndoSnapshot();
                if (b.count < 0) b.count = 0;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeBurst = i;
            ImGui::PopID();
        }
        if (removeBurst >= 0) {
            UndoSnapshot();
            ps.bursts.erase(ps.bursts.begin() + removeBurst);
        }
    }

    // Rigidbody2D inspector. velocity + gravity DragFloat2s,
    // drag DragFloat. All in world units / second.
    void DrawRigidbody2DEditor(SceneEditor::Entity& /*entity*/,
                                SceneEditor::Rigidbody2DComponent& rb) {
        float v[2] = { rb.velocity.x, rb.velocity.y };
        if (ImGui::DragFloat2("Velocity", v, 0.05f, 0.0f, 0.0f,
                               "%.2f")) {
            UndoSnapshot();
            rb.velocity = { v[0], v[1] };
        }
        float g[2] = { rb.gravity.x, rb.gravity.y };
        if (ImGui::DragFloat2("Gravity", g, 0.05f, 0.0f, 0.0f,
                               "%.2f")) {
            UndoSnapshot();
            rb.gravity = { g[0], g[1] };
        }
        if (ImGui::DragFloat("Drag", &rb.drag, 0.01f, 0.0f, 0.0f,
                              "%.2f")) {
            UndoSnapshot();
            if (rb.drag < 0.0f) rb.drag = 0.0f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Per-frame damping. velocity *= max(0, 1 - drag * dt)\n"
                "0 = none; ~5 stops a 1 u/s body in ~0.2 s.");
        }
        if (ImGui::DragFloat("Mass", &rb.mass, 0.05f, 0.01f, 0.0f,
                              "%.2f")) {
            UndoSnapshot();
            if (rb.mass <= 0.0f) rb.mass = 0.01f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Inertia. Collision push-out splits inversely\n"
                "with mass: a 1-kg ball hitting a 10-kg crate\n"
                "moves 10x as far as the crate does.");
        }
        if (ImGui::DragFloat("Restitution", &rb.restitution,
                              0.01f, 0.0f, 1.0f, "%.2f")) {
            UndoSnapshot();
            rb.restitution = std::clamp(rb.restitution, 0.0f, 1.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Bounciness [0..1]. 0 absorbs the inward velocity\n"
                "(M7c7 default); 1 reflects it perfectly.");
        }
        if (ImGui::DragFloat("Friction", &rb.friction,
                              0.01f, 0.0f, 1.0f, "%.2f")) {
            UndoSnapshot();
            rb.friction = std::clamp(rb.friction, 0.0f, 1.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Per-contact tangential damping [0..1]. 0 = the\n"
                "body slides along the surface unaffected;\n"
                "1 = tangent motion stops on contact.");
        }
    }

    // BoxCollider inspector. size + offset are DragFloat2s in
    // world units; isTrigger flips between "overlap-only event
    // surface" (M7c1 default) and the M7c10 solid response
    // (MTV push-out + restitution + friction routed through
    // the companion Rigidbody2D when present).
    void DrawBoxColliderEditor(SceneEditor::Entity& /*entity*/,
                               SceneEditor::BoxColliderComponent& bc) {
        float sz[2] = { bc.size.x, bc.size.y };
        if (ImGui::DragFloat2("Size", sz, 0.05f, 0.0f, 0.0f,
                               "%.2f")) {
            UndoSnapshot();
            bc.size = { sz[0], sz[1] };
        }
        float off[2] = { bc.offset.x, bc.offset.y };
        if (ImGui::DragFloat2("Offset", off, 0.05f, 0.0f, 0.0f,
                               "%.2f")) {
            UndoSnapshot();
            bc.offset = { off[0], off[1] };
        }
        bool trig = bc.isTrigger;
        if (ImGui::Checkbox("Is Trigger", &trig)) {
            UndoSnapshot();
            bc.isTrigger = trig;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "When set, overlap fires OnTriggerEnter / Exit\n"
                "without a physical response. When unset, the\n"
                "M7c10 solid response runs: MTV push-out split\n"
                "by inverse mass + bounce by combined\n"
                "restitution + tangent damping by combined\n"
                "friction (via the companion Rigidbody2D).");
        }
        DrawColliderLayerCombo(bc.layer);
    }

    // Shared layer combo for both box + circle inspectors. Pulls
    // the live list from Project::CollisionLayers + writes the
    // selection back through UndoSnapshot.
    void DrawColliderLayerCombo(std::string& layer) {
        const auto& layers = m_project.CollisionLayers();
        if (ImGui::BeginCombo("Layer", layer.c_str())) {
            for (const std::string& name : layers) {
                const bool sel = (name == layer);
                if (ImGui::Selectable(name.c_str(), sel)) {
                    UndoSnapshot();
                    layer = name;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Collision layer. Pairs whose two layers are\n"
                "un-ticked in Edit > Collision Matrix are\n"
                "filtered out of every overlap test.");
        }
    }

    // Capsule = radius + halfLength (spine along local Y). The
    // collision pass decomposes it into a centre rectangle +
    // two end-cap circles (M7c27).
    void DrawCapsuleColliderEditor(SceneEditor::Entity& /*entity*/,
                                   SceneEditor::CapsuleColliderComponent& cc) {
        if (ImGui::DragFloat("Radius", &cc.radius, 0.05f,
                              0.0f, 0.0f, "%.2f")) {
            UndoSnapshot();
            if (cc.radius < 0.0f) cc.radius = 0.0f;
        }
        if (ImGui::DragFloat("Half Length", &cc.halfLength, 0.05f,
                              0.0f, 0.0f, "%.2f")) {
            UndoSnapshot();
            if (cc.halfLength < 0.0f) cc.halfLength = 0.0f;
        }
        float off[2] = { cc.offset.x, cc.offset.y };
        if (ImGui::DragFloat2("Offset", off, 0.05f, 0.0f, 0.0f,
                               "%.2f")) {
            UndoSnapshot();
            cc.offset = { off[0], off[1] };
        }
        bool trig = cc.isTrigger;
        if (ImGui::Checkbox("Is Trigger", &trig)) {
            UndoSnapshot();
            cc.isTrigger = trig;
        }
        DrawColliderLayerCombo(cc.layer);
    }

    // Edge / chain (M7c32). One DragFloat2 per vertex + Add
    // / Remove buttons + a closed-loop checkbox. Two vertices
    // is the minimum; sub-2 inputs get a no-op synthesis.
    void DrawEdgeColliderEditor(SceneEditor::Entity& /*entity*/,
                                SceneEditor::EdgeColliderComponent& ec) {
        ImGui::Text("Vertices (%d):", int(ec.vertices.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton("+##addvert_edge")) {
            UndoSnapshot();
            if (ec.vertices.size() >= 1) {
                const auto& a = ec.vertices.back();
                ec.vertices.push_back({ a.x + 0.5f, a.y });
            } else {
                ec.vertices.push_back({ 0.0f, 0.0f });
            }
        }
        int removeIdx = -1;
        for (size_t i = 0; i < ec.vertices.size(); ++i) {
            ImGui::PushID(int(i));
            float v[2] = { ec.vertices[i].x, ec.vertices[i].y };
            if (ImGui::DragFloat2("##ev", v, 0.05f, 0.0f, 0.0f,
                                   "%.2f")) {
                UndoSnapshot();
                ec.vertices[i] = { v[0], v[1] };
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x") && ec.vertices.size() > 2) {
                removeIdx = int(i);
            }
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            UndoSnapshot();
            ec.vertices.erase(ec.vertices.begin() + removeIdx);
        }
        bool closed = ec.closed;
        if (ImGui::Checkbox("Closed (loop)", &closed)) {
            UndoSnapshot();
            ec.closed = closed;
        }
        float off[2] = { ec.offset.x, ec.offset.y };
        if (ImGui::DragFloat2("Offset", off, 0.05f, 0.0f, 0.0f,
                               "%.2f")) {
            UndoSnapshot();
            ec.offset = { off[0], off[1] };
        }
        bool trig = ec.isTrigger;
        if (ImGui::Checkbox("Is Trigger", &trig)) {
            UndoSnapshot();
            ec.isTrigger = trig;
        }
        DrawColliderLayerCombo(ec.layer);
    }

    // Convex polygon (M7c28). One DragFloat2 per vertex +
    // Add / Remove buttons. The runtime trusts the user's
    // CCW convex input; concave polygons silently misbehave
    // through SAT.
    void DrawPolygonColliderEditor(SceneEditor::Entity& /*entity*/,
                                   SceneEditor::PolygonColliderComponent& pgc) {
        ImGui::Text("Vertices (%d):", int(pgc.vertices.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton("+##addvert")) {
            UndoSnapshot();
            // Append a vertex midway between the last + first
            // so the polygon's overall shape stays sensible.
            if (pgc.vertices.size() >= 2) {
                const auto& a = pgc.vertices.back();
                const auto& b = pgc.vertices.front();
                pgc.vertices.push_back(
                    { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f });
            } else {
                pgc.vertices.push_back({ 0.3f, 0.0f });
            }
        }
        int removeIdx = -1;
        for (size_t i = 0; i < pgc.vertices.size(); ++i) {
            ImGui::PushID(int(i));
            float v[2] = { pgc.vertices[i].x, pgc.vertices[i].y };
            if (ImGui::DragFloat2("##v", v, 0.05f, 0.0f, 0.0f,
                                   "%.2f")) {
                UndoSnapshot();
                pgc.vertices[i] = { v[0], v[1] };
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x") && pgc.vertices.size() > 3) {
                removeIdx = int(i);
            }
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            UndoSnapshot();
            pgc.vertices.erase(pgc.vertices.begin() + removeIdx);
        }
        float off[2] = { pgc.offset.x, pgc.offset.y };
        if (ImGui::DragFloat2("Offset", off, 0.05f, 0.0f, 0.0f,
                               "%.2f")) {
            UndoSnapshot();
            pgc.offset = { off[0], off[1] };
        }
        bool trig = pgc.isTrigger;
        if (ImGui::Checkbox("Is Trigger", &trig)) {
            UndoSnapshot();
            pgc.isTrigger = trig;
        }
        DrawColliderLayerCombo(pgc.layer);
    }

    // M8c8 AudioListener inspector. No editable fields; the
    // component is a marker so the runtime knows where to
    // anchor 3D attenuation + panning.
    void DrawAudioListenerEditor(SceneEditor::Entity& /*entity*/,
                                  SceneEditor::AudioListenerComponent& /*al*/) {
        ImGui::TextDisabled(
            "Audio reference point for 3D AudioSources.");
        ImGui::TextDisabled(
            "Only the first enabled listener is active.");
    }

    // M8c7 AudioSource inspector. Clip combo over the asset
    // DB's Audio entries + the standard 0-1 volume slider +
    // two policy checkboxes (Play On Start / Loop). Loop
    // routes through PlayMusic (streaming) since the M8c2
    // SFX cache is one-shot.
    void DrawAudioSourceEditor(SceneEditor::Entity& /*entity*/,
                                SceneEditor::AudioSourceComponent& as) {
        std::string current = "(none)";
        if (as.clip.IsValid()) {
            if (const auto* e = m_assetDb.FindByGuid(as.clip)) {
                current = e->relPath;
            } else {
                current = as.clip.ToString();
            }
        }
        if (ImGui::BeginCombo("Clip", current.c_str())) {
            if (ImGui::Selectable("(none)", !as.clip.IsValid())) {
                UndoSnapshot();
                as.clip = SceneEditor::AssetGuid{};
            }
            for (const SceneEditor::AssetEntry& a :
                     m_assetDb.Entries()) {
                if (a.typeName != "Audio") continue;
                const bool sel = (a.guid == as.clip);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    UndoSnapshot();
                    as.clip = a.guid;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::SliderFloat("Volume", &as.volume,
                                0.0f, 1.0f, "%.2f")) {
            UndoSnapshot();
            if (as.volume < 0.0f) as.volume = 0.0f;
            if (as.volume > 1.0f) as.volume = 1.0f;
        }
        bool pos = as.playOnStart;
        if (ImGui::Checkbox("Play On Start", &pos)) {
            UndoSnapshot();
            as.playOnStart = pos;
        }
        bool lp = as.loop;
        if (ImGui::Checkbox("Loop (streams via PlayMusic)", &lp)) {
            UndoSnapshot();
            as.loop = lp;
        }
        ImGui::TextDisabled(
            "Loop routes through the streaming voice; non-loop "
            "fires the cached SFX path.");
        // M8c8 3D-positional block. Hidden when is3D is off
        // so the inspector stays compact for 2D sources.
        bool is3D = as.is3D;
        if (ImGui::Checkbox("3D Positional", &is3D)) {
            UndoSnapshot();
            as.is3D = is3D;
        }
        if (as.is3D) {
            if (ImGui::DragFloat("Min Distance", &as.minDistance,
                                  0.05f, 0.0f, 0.0f, "%.2f")) {
                UndoSnapshot();
                if (as.minDistance < 0.0f) as.minDistance = 0.0f;
            }
            if (ImGui::DragFloat("Max Distance", &as.maxDistance,
                                  0.05f, 0.0f, 0.0f, "%.2f")) {
                UndoSnapshot();
                if (as.maxDistance < as.minDistance + 0.01f) {
                    as.maxDistance = as.minDistance + 0.01f;
                }
            }
            ImGui::TextDisabled(
                "Linear attenuation between min + max distance. "
                "Pan from horizontal offset / maxDistance.");
        }
    }

    // Same shape as DrawBoxColliderEditor but with a single
    // radius DragFloat instead of a Vec2 size.
    void DrawCircleColliderEditor(SceneEditor::Entity& /*entity*/,
                                  SceneEditor::CircleColliderComponent& cc) {
        if (ImGui::DragFloat("Radius", &cc.radius, 0.05f,
                              0.0f, 0.0f, "%.2f")) {
            UndoSnapshot();
            if (cc.radius < 0.0f) cc.radius = 0.0f;
        }
        float off[2] = { cc.offset.x, cc.offset.y };
        if (ImGui::DragFloat2("Offset", off, 0.05f, 0.0f, 0.0f,
                               "%.2f")) {
            UndoSnapshot();
            cc.offset = { off[0], off[1] };
        }
        bool trig = cc.isTrigger;
        if (ImGui::Checkbox("Is Trigger", &trig)) {
            UndoSnapshot();
            cc.isTrigger = trig;
        }
        DrawColliderLayerCombo(cc.layer);
    }

    // ScriptComponent inspector. Combo over every Script asset in
    // the project + an InputText for a per-entity entry function
    // override. Dropping a `.lua` asset on the combo assigns it
    // without opening the dropdown, mirroring the M5c35 pattern
    // the Controller Editor's animation combo uses.
    void DrawScriptEditor(SceneEditor::Entity& entity,
                          SceneEditor::ScriptComponent& sc) {
        bool changed = false;
        std::string current = "(none)";
        if (sc.script.IsValid()) {
            if (const auto* e = m_assetDb.FindByGuid(sc.script)) {
                current = e->relPath;
            } else {
                current = sc.script.ToString();
            }
        }
        if (ImGui::BeginCombo("Script", current.c_str())) {
            if (ImGui::Selectable("(none)", !sc.script.IsValid())) {
                UndoSnapshot();
                sc.script = SceneEditor::AssetGuid{};
                changed = true;
            }
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Script") continue;
                const bool sel = (a.guid == sc.script);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    UndoSnapshot();
                    sc.script = a.guid;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                if (p->DataSize == sizeof(SceneEditor::AssetGuid)) {
                    SceneEditor::AssetGuid guid;
                    std::memcpy(&guid, p->Data, sizeof(guid));
                    if (const auto* a = m_assetDb.FindByGuid(guid)) {
                        if (a->typeName == "Script") {
                            UndoSnapshot();
                            sc.script = guid;
                            changed = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        char fnBuf[128];
        std::snprintf(fnBuf, sizeof(fnBuf), "%s",
                      sc.entryFunction.c_str());
        if (ImGui::InputTextWithHint(
                "Entry function", "OnAnimationEvent (default)",
                fnBuf, sizeof(fnBuf))) {
            UndoSnapshot();
            sc.entryFunction = fnBuf;
            changed = true;
        }
        if (changed) (void)entity;  // M6c5 will wire dispatch routing
    }

    void DrawAnimatorEditor(SceneEditor::Entity& entity,
                            SceneEditor::AnimatorComponent& an) {
        bool changed = false;
        // Animation combo over every .nanim asset in the project.
        std::string current = "(none)";
        if (an.animation.IsValid()) {
            if (const auto* e = m_assetDb.FindByGuid(an.animation)) {
                current = e->relPath;
            } else {
                current = an.animation.ToString();
            }
        }
        if (ImGui::BeginCombo("Animation", current.c_str())) {
            if (ImGui::Selectable("(none)", !an.animation.IsValid())) {
                UndoSnapshot();
                an.animation = SceneEditor::AssetGuid{};
                an.currentTime = 0.0f;
                changed = true;
            }
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Animation") continue;
                const bool sel = (a.guid == an.animation);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    UndoSnapshot();
                    an.animation = a.guid;
                    an.currentTime = 0.0f;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                if (p->DataSize == sizeof(SceneEditor::AssetGuid)) {
                    SceneEditor::AssetGuid guid;
                    std::memcpy(&guid, p->Data, sizeof(guid));
                    if (const auto* e = m_assetDb.FindByGuid(guid)) {
                        if (e->typeName == "Animation") {
                            UndoSnapshot();
                            an.animation = guid;
                            an.currentTime = 0.0f;
                            changed = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        {
            bool draft = an.isPlaying;
            if (ImGui::Checkbox("Playing", &draft)) {
                UndoSnapshot();
                an.isPlaying = draft;
                changed = true;
            }
        }
        // Read-out so the user can see playback advancing live.
        if (SceneEditor::AnimationAsset* anim =
                m_assetDb.GetAnimation(an.animation)) {
            const float total = anim->TotalDuration();
            ImGui::TextDisabled("time %.2fs / %.2fs  (%zu frames)",
                                an.currentTime, total, anim->frames.size());
            if (const SceneEditor::AnimationFrame* fr =
                    anim->Sample(an.currentTime)) {
                ImGui::TextDisabled("current frame: %s",
                                    fr->sprite.c_str());
            }
        }
        if (ImGui::SmallButton("Restart")) {
            UndoSnapshot();
            an.currentTime = 0.0f;
            an.currentState = -1;
            changed = true;
        }

        // --- Controller ---------------------------------------------
        ImGui::Separator();
        std::string ctrlLabel = "(none)";
        if (an.controller.IsValid()) {
            if (const auto* e = m_assetDb.FindByGuid(an.controller)) {
                ctrlLabel = e->relPath;
            } else {
                ctrlLabel = an.controller.ToString();
            }
        }
        if (ImGui::BeginCombo("Controller", ctrlLabel.c_str())) {
            if (ImGui::Selectable("(none)", !an.controller.IsValid())) {
                UndoSnapshot();
                an.controller   = SceneEditor::AssetGuid{};
                an.currentState = -1;
                changed = true;
            }
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "AnimatorController") continue;
                const bool sel = (a.guid == an.controller);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    UndoSnapshot();
                    an.controller   = a.guid;
                    an.currentState = -1;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (an.controller.IsValid()) {
            if (SceneEditor::AnimatorControllerAsset* ctrl =
                    m_assetDb.GetAnimatorController(an.controller)) {
                if (an.currentState >= 0 &&
                    an.currentState < int(ctrl->states.size())) {
                    ImGui::TextDisabled("Current state: %s",
                        ctrl->states[size_t(an.currentState)].name.c_str());
                } else {
                    ImGui::TextDisabled("Current state: (uninitialised)");
                }
                // Parameter controls. Per-type widget; missing live
                // entries fall back to the controller's declared
                // defaults so a fresh animator still starts from a
                // known position.
                if (!ctrl->parameters.empty()) {
                    ImGui::Text("Parameters");
                    {
                        const char* typeFilterNames[] = {
                            "All", "Bool", "Float", "Int", "Trigger",
                            "Vec2", "Color", "String" };
                        ImGui::SetNextItemWidth(90.0f);
                        ImGui::Combo("##animparamtype",
                                     &m_animInspectorParamTypeFilter,
                                     typeFilterNames, 8);
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputTextWithHint(
                            "##animparamfilter", "Filter parameters...",
                            m_animInspectorParamFilter,
                            sizeof(m_animInspectorParamFilter));
                    }
                    using PT = SceneEditor::AnimatorParameter::Type;
                    for (const auto& p : ctrl->parameters) {
                        if (!ParamTypeMatchesFilter(
                                p.type, m_animInspectorParamTypeFilter)) {
                            continue;
                        }
                        if (!ParamNameMatchesFilter(
                                p.name, m_animInspectorParamFilter)) {
                            continue;
                        }
                        if (p.type == PT::Float) {
                            float v = p.defaultFloat;
                            auto it = an.paramFloats.find(p.name);
                            if (it != an.paramFloats.end()) v = it->second;
                            if (ImGui::DragFloat(p.name.c_str(), &v, 0.05f)) {
                                UndoSnapshot();
                                an.paramFloats[p.name] = v;
                                changed = true;
                            }
                        } else if (p.type == PT::Int) {
                            int v = p.defaultInt;
                            auto it = an.paramInts.find(p.name);
                            if (it != an.paramInts.end()) v = it->second;
                            if (ImGui::DragInt(p.name.c_str(), &v)) {
                                UndoSnapshot();
                                an.paramInts[p.name] = v;
                                changed = true;
                            }
                        } else if (p.type == PT::Trigger) {
                            // Fire-once button. Reads "armed" when the
                            // live value is already true, "fire" when
                            // it's false. Press flips to true and the
                            // runtime resets it the next tick.
                            auto it = an.paramBools.find(p.name);
                            const bool armed =
                                (it != an.paramBools.end()) && it->second;
                            char label[128];
                            std::snprintf(label, sizeof(label),
                                          armed ? "%s (armed)" : "Fire %s",
                                          p.name.c_str());
                            if (ImGui::Button(label)) {
                                UndoSnapshot();
                                an.paramBools[p.name] = true;
                                changed = true;
                            }
                        } else if (p.type == PT::Vec2) {
                            NaraGame::Vec2 vv = p.defaultVec2;
                            auto it = an.paramVec2.find(p.name);
                            if (it != an.paramVec2.end()) vv = it->second;
                            float v2[2] = { vv.x, vv.y };
                            if (ImGui::DragFloat2(p.name.c_str(), v2, 0.05f)) {
                                UndoSnapshot();
                                an.paramVec2[p.name] = { v2[0], v2[1] };
                                changed = true;
                            }
                        } else if (p.type == PT::Color) {
                            NaraGame::Color cc = p.defaultColor;
                            auto it = an.paramColors.find(p.name);
                            if (it != an.paramColors.end()) cc = it->second;
                            float col[4] = { cc.r, cc.g, cc.b, cc.a };
                            if (ImGui::ColorEdit4(p.name.c_str(), col)) {
                                UndoSnapshot();
                                an.paramColors[p.name] = {
                                    col[0], col[1], col[2], col[3] };
                                changed = true;
                            }
                        } else if (p.type == PT::String) {
                            std::string cur = p.defaultString;
                            auto it = an.paramStrings.find(p.name);
                            if (it != an.paramStrings.end()) cur = it->second;
                            char buf[256];
                            std::snprintf(buf, sizeof(buf), "%s",
                                          cur.c_str());
                            if (ImGui::InputText(p.name.c_str(),
                                                  buf, sizeof(buf))) {
                                UndoSnapshot();
                                an.paramStrings[p.name] = buf;
                                changed = true;
                            }
                        } else {
                            bool draft = p.defaultBool;
                            auto it = an.paramBools.find(p.name);
                            if (it != an.paramBools.end()) draft = it->second;
                            if (ImGui::Checkbox(p.name.c_str(), &draft)) {
                                UndoSnapshot();
                                an.paramBools[p.name] = draft;
                                changed = true;
                            }
                        }
                    }
                }
            }
        }
        if (changed) {
            m_scene.MarkDirty();
            UpdateWindowTitle();
        }
        (void)entity;
    }

    void DrawTilemapEditor(SceneEditor::Entity& entity,
                           SceneEditor::TilemapComponent& tm) {
        bool changed = false;
        // Tileset (source texture) Combo over every Texture asset in
        // the project, mirroring the SpriteRenderer sprite picker.
        std::string current = "(none)";
        if (tm.tileset.IsValid()) {
            if (const auto* e = m_assetDb.FindByGuid(tm.tileset)) {
                current = e->relPath;
            } else {
                current = tm.tileset.ToString();
            }
        }
        if (ImGui::BeginCombo("Tileset", current.c_str())) {
            if (ImGui::Selectable("(none)", !tm.tileset.IsValid())) {
                UndoSnapshot();
                tm.tileset = SceneEditor::AssetGuid{};
                changed = true;
            }
            // .ntileset assets first (the higher-level form), then raw
            // textures as a fallback for the M4c1 path.
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Tileset") continue;
                const bool sel = (a.guid == tm.tileset);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    UndoSnapshot();
                    tm.tileset = a.guid;
                    changed = true;
                }
            }
            ImGui::Separator();
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Texture") continue;
                const bool sel = (a.guid == tm.tileset);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    UndoSnapshot();
                    tm.tileset = a.guid;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                if (p->DataSize == sizeof(SceneEditor::AssetGuid)) {
                    SceneEditor::AssetGuid guid;
                    std::memcpy(&guid, p->Data, sizeof(guid));
                    if (const auto* e = m_assetDb.FindByGuid(guid)) {
                        if (e->typeName == "Texture" ||
                            e->typeName == "Tileset") {
                            UndoSnapshot();
                            tm.tileset = guid;
                            changed = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        // Cell Pixels only matters when the tileset GUID points at a
        // raw Texture. When it points at a .ntileset asset, the
        // tileset file owns its own cellPixelW/H and these fields are
        // greyed out as a hint.
        const bool tilesetOwnedCells =
            tm.tileset.IsValid() &&
            m_assetDb.FindByGuid(tm.tileset) &&
            m_assetDb.FindByGuid(tm.tileset)->typeName == "Tileset";
        if (tilesetOwnedCells) ImGui::BeginDisabled();
        int sz[2] = { tm.cellPixelW, tm.cellPixelH };
        if (ImGui::DragInt2("Cell Pixels (W,H)", sz, 0.2f, 1, 4096)) {
            tm.cellPixelW = std::max(1, sz[0]);
            tm.cellPixelH = std::max(1, sz[1]);
            changed = true;
        }
        if (tilesetOwnedCells) ImGui::EndDisabled();
        if (ImGui::IsItemActivated()) UndoSnapshot();

        // Sorting layer + order, mirroring the SpriteRenderer inspector
        // so the unified sort pass can place tilemaps anywhere in the
        // stack.
        const auto& layers = m_project.SortingLayers();
        const bool currentInList = std::find(layers.begin(), layers.end(),
                                              tm.sortingLayer) != layers.end();
        if (ImGui::BeginCombo("Sorting Layer", tm.sortingLayer.c_str())) {
            if (!currentInList && !tm.sortingLayer.empty()) {
                if (ImGui::Selectable((tm.sortingLayer + "  (orphan)").c_str(),
                                      true)) {
                    // No-op; clicking the orphan keeps it.
                }
                ImGui::Separator();
            }
            for (const std::string& l : layers) {
                const bool sel = (l == tm.sortingLayer);
                if (ImGui::Selectable(l.c_str(), sel)) {
                    UndoSnapshot();
                    tm.sortingLayer = l;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::DragInt("Order in Layer", &tm.orderInLayer, 0.2f,
                           -1000, 1000)) {
            changed = true;
        }
        if (ImGui::IsItemActivated()) UndoSnapshot();
        // M7c20: collision layer for the M7c18 per-tile collider
        // synthesis pass. Combo mirrors the BoxCollider /
        // CircleCollider layer picker; the runtime feeds the
        // selected layer into Project::CollisionLayersCollide.
        const auto& cLayers = m_project.CollisionLayers();
        if (ImGui::BeginCombo("Collision Layer",
                               tm.collisionLayer.c_str())) {
            for (const std::string& l : cLayers) {
                const bool sel = (l == tm.collisionLayer);
                if (ImGui::Selectable(l.c_str(), sel)) {
                    UndoSnapshot();
                    tm.collisionLayer = l;
                    changed = true;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Collision layer for every solid / trigger\n"
                "tile this Tilemap synthesises. Filtered by\n"
                "Window > Collision Matrix.");
        }
        ImGui::TextDisabled("Painted cells: %zu", tm.cells.size());
        if (changed) {
            m_scene.MarkDirty();
            UpdateWindowTitle();
        }
        (void)entity;
    }

    // Asset Inspector: shown when the user clicks a file in the Project
    // panel and no entity is selected. Texture / Audio have full
    // import-settings editors (M2c3 / M8c6); M11c5 adds quick-glance
    // metadata + "Open in editor" buttons for tileset / autotile /
    // animation / controller / script / scene + a file-size readout
    // for unrecognised types so every asset shows something useful.
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
        // On-disk size readout. Cheap + useful for every type --
        // a quick way to spot the giant texture in the project.
        {
            std::error_code ec;
            const std::string abs =
                m_assetDb.AssetsRoot() + "/" + entry->relPath;
            const auto sz = std::filesystem::file_size(abs, ec);
            if (!ec) {
                if (sz < 1024) {
                    ImGui::TextDisabled("Size:   %llu B",
                        static_cast<unsigned long long>(sz));
                } else if (sz < 1024 * 1024) {
                    ImGui::TextDisabled("Size:   %.1f KB",
                        double(sz) / 1024.0);
                } else {
                    ImGui::TextDisabled("Size:   %.2f MB",
                        double(sz) / (1024.0 * 1024.0));
                }
            }
        }
        ImGui::Separator();
        if (entry->typeName == "Texture") {
            DrawTextureImportEditor(*entry);
        } else if (entry->typeName == "Audio") {
            DrawAudioImportEditor(*entry);
        } else if (entry->typeName == "Tileset") {
            DrawTilesetAssetInspector(*entry);
        } else if (entry->typeName == "Autotile") {
            DrawAutotileAssetInspector(*entry);
        } else if (entry->typeName == "Animation") {
            DrawAnimationAssetInspector(*entry);
        } else if (entry->typeName == "AnimatorController") {
            DrawControllerAssetInspector(*entry);
        } else if (entry->typeName == "Script") {
            DrawScriptAssetInspector(*entry);
        } else if (entry->typeName == "Scene") {
            DrawSceneAssetInspector(*entry);
        } else {
            ImGui::TextDisabled("(no inspector for %s yet)",
                                entry->typeName.c_str());
        }
    }

    // M11c5 lightweight per-type asset inspectors. Each one shows a
    // few headline stats + an "Open in <X> Editor" button that
    // routes through the existing open-* helpers. The dedicated
    // editor windows are still the place to actually edit; this is
    // a quick-glance view + an explicit way to jump in.
    void DrawTilesetAssetInspector(SceneEditor::AssetEntry& entry) {
        if (auto* ts = m_assetDb.GetTileset(entry.guid)) {
            ImGui::Text("Cell:   %d x %d px",
                ts->cellPixelW, ts->cellPixelH);
            ImGui::Text("Tiles:  %zu defined", ts->tiles.size());
            ImGui::TextDisabled("Source texture: %s",
                ts->texture.IsValid()
                    ? ts->texture.ToString().c_str() : "(unset)");
        } else {
            ImGui::TextDisabled("(failed to load tileset)");
        }
        if (ImGui::Button("Open in Tileset Editor")) {
            OpenTilesetEditor(entry.guid);
        }
    }

    void DrawAutotileAssetInspector(SceneEditor::AssetEntry& entry) {
        if (auto* at = m_assetDb.GetAutotile(entry.guid)) {
            ImGui::Text("Mode:  %s",
                at->mode == SceneEditor::AutotileAsset::Mode::Cardinal
                    ? "Cardinal (4-bit)" : "Blob (8-bit)");
            ImGui::Text("Cell:  %d x %d px",
                at->cellPixelW, at->cellPixelH);
            ImGui::Text("Tiles: %zu unique", at->tileSet.size());
            ImGui::TextDisabled("Source texture: %s",
                at->texture.IsValid()
                    ? at->texture.ToString().c_str() : "(unset)");
        } else {
            ImGui::TextDisabled("(failed to load autotile)");
        }
        if (ImGui::Button("Open in Autotile Editor")) {
            OpenAutotileEditor(entry.guid);
        }
    }

    void DrawAnimationAssetInspector(SceneEditor::AssetEntry& entry) {
        if (auto* an = m_assetDb.GetAnimation(entry.guid)) {
            ImGui::Text("Frames: %zu", an->frames.size());
            ImGui::Text("Loop:   %s", an->loop ? "yes" : "no");
            ImGui::Text("Events: %zu", an->events.size());
            ImGui::TextDisabled("Total: %.2f s",
                double(an->TotalDuration()));
            if (!an->texture.IsValid()) {
                ImGui::TextDisabled("(no source texture set)");
            }
        } else {
            ImGui::TextDisabled("(failed to load animation)");
        }
        if (ImGui::Button("Open in Animation Editor")) {
            OpenAnimationEditor(entry.guid);
        }
    }

    void DrawControllerAssetInspector(SceneEditor::AssetEntry& entry) {
        if (auto* ctrl = m_assetDb.GetAnimatorController(entry.guid)) {
            ImGui::Text("States:      %zu", ctrl->states.size());
            ImGui::Text("Transitions: %zu", ctrl->transitions.size());
            ImGui::Text("Parameters:  %zu", ctrl->parameters.size());
            if (ctrl->defaultState >= 0 &&
                size_t(ctrl->defaultState) < ctrl->states.size()) {
                ImGui::TextDisabled("Default state: %s",
                    ctrl->states[size_t(ctrl->defaultState)]
                        .name.c_str());
            }
        } else {
            ImGui::TextDisabled("(failed to load controller)");
        }
        if (ImGui::Button("Open in Controller Editor")) {
            OpenControllerEditor(entry.guid);
        }
    }

    void DrawScriptAssetInspector(SceneEditor::AssetEntry& entry) {
        // No deep parse -- the script editor handles that. We do
        // show the first non-blank line so the inspector hints at
        // the script's purpose without round-tripping into the
        // editor window.
        const std::string abs =
            m_assetDb.AssetsRoot() + "/" + entry.relPath;
        std::ifstream in(abs);
        std::string firstLine;
        while (in && firstLine.empty()) {
            if (!std::getline(in, firstLine)) break;
            // Trim leading whitespace.
            size_t i = 0;
            while (i < firstLine.size() &&
                   (firstLine[i] == ' ' || firstLine[i] == '\t')) ++i;
            firstLine.erase(0, i);
        }
        if (firstLine.empty()) firstLine = "(empty)";
        ImGui::TextWrapped("First line: %s", firstLine.c_str());
        if (ImGui::Button("Open in Script Editor")) {
            OpenScriptEditor(abs, entry.relPath);
        }
    }

    void DrawSceneAssetInspector(SceneEditor::AssetEntry& entry) {
        ImGui::TextDisabled(
            "Scene files open via File > Open Scene...");
        if (ImGui::Button("Load this scene")) {
            // Same path the menu uses; warns if dirty, etc.
            const std::string abs =
                m_assetDb.AssetsRoot() + "/" + entry.relPath;
            NotifyAllScriptsForDestroy();
            if (m_scene.Load(abs)) {
                m_currentScenePath = abs;
                m_selection = SceneEditor::kInvalidEntityId;
                UndoClear();
                UpdateWindowTitle();
            }
        }
    }

    // M8c6 audio asset preview. Lets the user audition a
    // sound effect or music track from the Inspector + tune
    // a default volume the project layer can wire to the
    // play call. The preview routes through Audio::Play /
    // Audio::PlayMusic + Audio::Stop, so what the user
    // hears here is what runtime scripts get.
    void DrawAudioImportEditor(SceneEditor::AssetEntry& entry) {
        // Cache the editor-side preview volume per asset
        // GUID so opening another sound + coming back keeps
        // the previous gain. The map lives as a member
        // (m_audioPreviewVolume) so it survives Inspector
        // redraws without leaking across project switches
        // (the asset DB swap clears all GUIDs we care about
        // when the user opens another project).
        float& vol = m_audioPreviewVolume[entry.guid];
        if (vol <= 0.0f) vol = 1.0f;
        ImGui::SliderFloat("Volume", &vol, 0.0f, 1.0f, "%.2f");
        // Build the absolute path the same way the Lua
        // binding does so a Project-pane preview matches
        // a script-driven play.
        const std::string& root = m_assetDb.AssetsRoot();
        std::string abs = root;
        if (!abs.empty() &&
            abs.back() != '/' && abs.back() != '\\') {
            abs.push_back('/');
        }
        abs.append(entry.relPath);
        if (ImGui::Button("Play")) {
            SceneEditor::Audio::Play(abs, vol);
        }
        ImGui::SameLine();
        if (ImGui::Button("Play (Music)")) {
            SceneEditor::Audio::PlayMusic(abs, vol, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            SceneEditor::Audio::Stop();
            SceneEditor::Audio::StopMusic();
        }
        // Honest hint about which decoder will run.
        const std::string& rel = entry.relPath;
        const auto dotPos = rel.find_last_of('.');
        std::string ext = (dotPos != std::string::npos)
            ? rel.substr(dotPos) : std::string{};
        std::transform(ext.begin(), ext.end(), ext.begin(),
                        [](unsigned char c) {
                            return char(std::tolower(c));
                        });
        const char* decoder = "?";
        if      (ext == ".wav") decoder = "PCM (RIFF)";
        else if (ext == ".ogg") decoder = "stb_vorbis";
        else if (ext == ".mp3") decoder = "minimp3";
        else                     decoder = "unsupported";
        ImGui::TextDisabled("Decoder: %s", decoder);
        ImGui::TextDisabled(
            "Tip: drag this asset onto an audio.play call in "
            "Lua to use its GUID:");
        ImGui::TextDisabled("  audio.play(\"%s\")",
                             entry.guid.ToString().c_str());
    }

    void DrawTextureImportEditor(SceneEditor::AssetEntry& entry) {
        // Decode the entry's existing importSettings, edit through the
        // struct form, then write back on any change. We track the
        // dirty flag explicitly (instead of memcmp) because the
        // spriteSheet vector has separate heap allocations per copy
        // and memcmp would trip every frame on any sliced texture.
        SceneEditor::TextureImportSettings imp;
        imp.FromJson(entry.importSettings);
        const auto prevFilter = imp.filterMode;
        bool changed = false;

        changed |= ImGui::DragFloat("Pixels Per Unit", &imp.pixelsPerUnit,
                                    0.5f, 1.0f, 4096.0f, "%.2f");
        const char* pivotNames[] = { "Center", "TopLeft", "TopRight",
                                     "BottomLeft", "BottomRight",
                                     "Custom" };
        int pivotIdx = int(imp.pivotMode);
        if (ImGui::Combo("Pivot", &pivotIdx, pivotNames, 6)) {
            imp.pivotMode = static_cast<
                SceneEditor::TextureImportSettings::PivotMode>(pivotIdx);
            changed = true;
        }
        if (imp.pivotMode ==
            SceneEditor::TextureImportSettings::PivotMode::Custom) {
            float cp[2] = { imp.customPivot.x, imp.customPivot.y };
            if (ImGui::DragFloat2("Custom Pivot", cp, 0.01f, 0.0f, 1.0f)) {
                imp.customPivot = { cp[0], cp[1] };
                changed = true;
            }
        }
        const char* filterNames[] = { "Linear", "Point" };
        int filterIdx = int(imp.filterMode);
        if (ImGui::Combo("Filter", &filterIdx, filterNames, 2)) {
            imp.filterMode = static_cast<
                SceneEditor::TextureImportSettings::Filter>(filterIdx);
            changed = true;
        }
        const char* wrapNames[] = { "Clamp", "Repeat", "Mirror" };
        int wrapIdx = int(imp.wrapMode);
        if (ImGui::Combo("Wrap", &wrapIdx, wrapNames, 3)) {
            imp.wrapMode = static_cast<
                SceneEditor::TextureImportSettings::Wrap>(wrapIdx);
            changed = true;
        }
        if (ImGui::Checkbox("Alpha is Transparency",
                            &imp.alphaIsTransparency)) {
            changed = true;
        }
        // 9-slice border (pixels). Edits here feed the Sliced draw mode
        // on SpriteRenderers that point at this texture without a
        // sub-sprite -- per-sub-sprite borders are edited in the Sprite
        // Editor instead.
        int border[4] = { imp.borderLeft, imp.borderRight,
                          imp.borderTop,  imp.borderBottom };
        if (ImGui::DragInt4("Border (L,R,T,B)", border, 0.2f, 0, 4096)) {
            imp.borderLeft   = border[0];
            imp.borderRight  = border[1];
            imp.borderTop    = border[2];
            imp.borderBottom = border[3];
            changed = true;
        }

        if (changed) {
            entry.importSettings = imp.ToJson();
            m_assetDb.SaveMeta(entry.guid);
            // FilterMode is sampler state baked into the texture object;
            // evict so the next GetTexture picks up the new setting.
            // PPU + pivot + border are read live by DrawSpriteEntity
            // each frame, no eviction needed.
            if (imp.filterMode != prevFilter) {
                m_assetDb.EvictTexture(entry.guid);
            }
        }
    }

    void DrawTransformEditor(SceneEditor::TransformComponent& t) {
        float pos[3]   = { t.position.x, t.position.y, t.position.z };
        float scale[3] = { t.scale.x,    t.scale.y,    t.scale.z };
        bool changed = false;
        // IsItemActivated fires on the frame a Drag widget gains focus,
        // BEFORE the first delta is applied -- the right place to
        // snapshot for undo. The same pattern is repeated for every
        // drag widget in the inspector.
        if (ImGui::DragFloat3("Position", pos, 0.05f)) {
            t.position = { pos[0], pos[1], pos[2] };
            changed = true;
        }
        if (ImGui::IsItemActivated()) UndoSnapshot();
        if (ImGui::DragFloat("Rotation Z", &t.rotation, 0.5f,
                             -3600.0f, 3600.0f, "%.2f deg")) {
            changed = true;
        }
        if (ImGui::IsItemActivated()) UndoSnapshot();
        if (ImGui::DragFloat3("Scale", scale, 0.01f, -1000.0f, 1000.0f)) {
            t.scale = { scale[0], scale[1], scale[2] };
            changed = true;
        }
        if (ImGui::IsItemActivated()) UndoSnapshot();
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
                UndoSnapshot();
                s.sprite = SceneEditor::AssetGuid{};
                changed = true;
            }
            for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
                if (a.typeName != "Texture") continue;
                const bool sel = (a.guid == s.sprite);
                if (ImGui::Selectable(a.relPath.c_str(), sel)) {
                    UndoSnapshot();
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
                            UndoSnapshot();
                            s.sprite = guid;
                            changed = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Sub-sprite picker: only meaningful when the bound texture has
        // a Multiple-mode sprite sheet. The "(whole texture)" entry is
        // always there as the unsliced default; an orphan name pinned
        // at the top lets the user keep it or swap to a live one.
        if (s.sprite.IsValid()) {
            if (const auto* entry = m_assetDb.FindByGuid(s.sprite)) {
                SceneEditor::TextureImportSettings imp;
                imp.FromJson(entry->importSettings);
                const auto& subs = imp.spriteSheet.sprites;
                if (!subs.empty()) {
                    bool nameInList = s.subSprite.empty();
                    for (const auto& sub : subs) {
                        if (sub.name == s.subSprite) { nameInList = true; break; }
                    }
                    const char* preview = s.subSprite.empty()
                        ? "(whole texture)" : s.subSprite.c_str();
                    if (ImGui::BeginCombo("Sub-sprite", preview)) {
                        if (!nameInList && !s.subSprite.empty()) {
                            if (ImGui::Selectable((s.subSprite + "  (orphan)").c_str(),
                                                  true)) {
                                // No-op; keep the orphan.
                            }
                            ImGui::Separator();
                        }
                        if (ImGui::Selectable("(whole texture)",
                                              s.subSprite.empty())) {
                            UndoSnapshot();
                            s.subSprite.clear();
                            changed = true;
                        }
                        for (const auto& sub : subs) {
                            const bool sel = (sub.name == s.subSprite);
                            if (ImGui::Selectable(sub.name.c_str(), sel)) {
                                UndoSnapshot();
                                s.subSprite = sub.name;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
        }
        float col[4] = { s.color.r, s.color.g, s.color.b, s.color.a };
        if (ImGui::ColorEdit4("Tint", col)) {
            s.color = { col[0], col[1], col[2], col[3] };
            changed = true;
        }
        if (ImGui::IsItemActivated()) UndoSnapshot();
        // Checkbox edits its target on the same frame it returns true,
        // so route through a local draft and snapshot before commit.
        {
            bool draft = s.flipX;
            if (ImGui::Checkbox("Flip X", &draft)) {
                UndoSnapshot();
                s.flipX = draft;
                changed = true;
            }
        }
        {
            bool draft = s.flipY;
            if (ImGui::Checkbox("Flip Y", &draft)) {
                UndoSnapshot();
                s.flipY = draft;
                changed = true;
            }
        }
        const char* modes[] = { "Simple", "Sliced", "Tiled" };
        int idx = int(s.drawMode);
        if (ImGui::Combo("Draw Mode", &idx, modes, 3)) {
            UndoSnapshot();
            s.drawMode = static_cast<SceneEditor::SpriteDrawMode>(idx);
            changed = true;
        }
        if (s.drawMode != SceneEditor::SpriteDrawMode::Simple) {
            float sz[2] = { s.size.x, s.size.y };
            if (ImGui::DragFloat2("Size", sz, 0.05f, 0.0f, 1024.0f)) {
                s.size = { sz[0], sz[1] };
                changed = true;
            }
            if (ImGui::IsItemActivated()) UndoSnapshot();
        }
        // Sorting layer dropdown -- if the entity is on a layer that no
        // longer exists in the project (renamed / deleted via the
        // Window > Sorting Layers... dialog) we still show the orphan
        // name at the top so the user can either keep it or pick one
        // from the live list.
        const auto& layers = m_project.SortingLayers();
        const bool currentInList = std::find(layers.begin(), layers.end(),
                                              s.sortingLayer) != layers.end();
        if (ImGui::BeginCombo("Sorting Layer", s.sortingLayer.c_str())) {
            if (!currentInList && !s.sortingLayer.empty()) {
                if (ImGui::Selectable((s.sortingLayer + "  (orphan)").c_str(),
                                      true)) {
                    // No-op; clicking the orphan keeps it.
                }
                ImGui::Separator();
            }
            for (const std::string& l : layers) {
                const bool sel = (l == s.sortingLayer);
                if (ImGui::Selectable(l.c_str(), sel)) {
                    UndoSnapshot();
                    s.sortingLayer = l;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::DragInt("Order in Layer", &s.orderInLayer, 0.2f,
                           -1000, 1000)) {
            changed = true;
        }
        if (ImGui::IsItemActivated()) UndoSnapshot();
        if (ImGui::SmallButton("Remove##sprite")) {
            UndoSnapshot();
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

    // M9c11 active-camera resolver. Walks the scene + returns
    // the enabled CameraComponent entity with the highest
    // `priority`. Ties break in scene-traversal order. Returns
    // nullptr when no camera component is present + enabled --
    // the standalone path then falls back to the last known
    // viewport state, the editor path keeps its user-driven
    // m_viewCam unchanged.
    SceneEditor::Entity* ResolveActiveCameraEntity() {
        std::vector<SceneEditor::EntityId> ids;
        ids.reserve(64);
        CollectAllEntities(m_scene.Roots(), ids);
        SceneEditor::Entity* best     = nullptr;
        int                  bestPrio = 0;
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            auto* cam = e->GetComponent<
                SceneEditor::CameraComponent>();
            if (!cam) continue;
            if (!best || cam->priority > bestPrio) {
                best     = e;
                bestPrio = cam->priority;
            }
        }
        return best;
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
        // M9c1 standalone: full-window borderless viewport. The
        // editor's toolbar (Reset View / Frame Selected / Play
        // checkbox) is suppressed -- a shipped game doesn't have
        // those controls. The window covers the whole client
        // area + skips title bar / resize affordances so
        // ImGui's draw list lands directly on the back buffer.
        if (m_standalone) {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            const ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                 ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::Begin("##StandaloneViewport", nullptr, flags);
            ImGui::PopStyleVar(2);
        } else {
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
            ImGui::SameLine();
            ImGui::Checkbox("Play", &m_animPlaying);
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

        // Skip the editor-only chrome in standalone mode: no
        // grid, no selection gizmo. The viewport just paints
        // the scene + lets gameplay drive what the player sees.
        if (!m_standalone) DrawGrid(dl, panelMin, panelSize);
        DrawEntities(dl, panelMin, panelSize);
        if (!m_standalone) DrawGizmo(dl, panelMin, panelSize);
        // M10c2 splash overlay paints on top of every entity so a
        // running fade visually covers the scene underneath.
        DrawSplashOverlay(dl, panelMin, panelSize);
        // M10c3 scene transition fade sits ON TOP of the splash so
        // a fade-to-black during scene swap covers everything,
        // including any in-flight splash from the outgoing scene.
        DrawTransitionOverlay(dl, panelMin, panelSize);

        dl->PopClipRect();
        ImGui::End();
    }

    // M10c2 splash overlay. Picks the active SplashScreenComponent
    // (highest priority among enabled), computes its current alpha
    // from the timeline, + paints a tinted background fill + the
    // splash texture stretched to the viewport panel. If no splash
    // exists or the active one is fully transparent, this is a
    // free no-op (no draw commands emitted).
    void DrawSplashOverlay(ImDrawList* dl,
                            const ImVec2& panelMin,
                            const ImVec2& panelSize) {
        std::vector<SceneEditor::EntityId> ids;
        ids.reserve(8);
        CollectAllEntities(m_scene.Roots(), ids);
        SceneEditor::SplashScreenComponent* best = nullptr;
        int bestPriority = INT_MIN;
        for (SceneEditor::EntityId id : ids) {
            const auto* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            auto* sp = const_cast<SceneEditor::Entity*>(e)->GetComponent<
                SceneEditor::SplashScreenComponent>();
            if (!sp) continue;
            if (sp->priority > bestPriority) {
                bestPriority = sp->priority;
                best         = sp;
            }
        }
        if (!best) return;
        const float alpha = best->CurrentAlpha();
        if (alpha <= 0.0f) return;
        const ImVec2 vMax{panelMin.x + panelSize.x,
                          panelMin.y + panelSize.y};
        // Background fill -- color * timeline alpha so a fade-to-
        // black between scenes still tints the underlying viewport
        // even when the texture is unset.
        const ImU32 bg = IM_COL32(
            int(best->background.r * 255.0f + 0.5f),
            int(best->background.g * 255.0f + 0.5f),
            int(best->background.b * 255.0f + 0.5f),
            int(best->background.a * alpha * 255.0f + 0.5f));
        if ((bg >> 24) != 0) {
            dl->AddRectFilled(panelMin, vMax, bg);
        }
        if (!best->texture.IsValid()) return;
        const auto* tex = m_assetDb.GetTexture(GetDevice(), best->texture);
        if (!tex) return;
        const float texW = float(tex->Width());
        const float texH = float(tex->Height());
        if (texW <= 0.0f || texH <= 0.0f) return;
        // Aspect-correct fit: letterbox / pillarbox the texture into
        // the viewport so a 16:9 splash on a 21:9 window doesn't
        // distort, it just leaves the panel's background showing on
        // the sides. Computed from the inner panel rect.
        const float panelAspect = panelSize.x / panelSize.y;
        const float texAspect   = texW / texH;
        float drawW = panelSize.x;
        float drawH = panelSize.y;
        if (texAspect > panelAspect) {
            // texture wider than panel -> fit to width, shrink height
            drawH = panelSize.x / texAspect;
        } else {
            drawW = panelSize.y * texAspect;
        }
        const ImVec2 drawMin{
            panelMin.x + (panelSize.x - drawW) * 0.5f,
            panelMin.y + (panelSize.y - drawH) * 0.5f};
        const ImVec2 drawMax{drawMin.x + drawW, drawMin.y + drawH};
        const ImU32 tint = IM_COL32(
            int(best->tint.r * 255.0f + 0.5f),
            int(best->tint.g * 255.0f + 0.5f),
            int(best->tint.b * 255.0f + 0.5f),
            int(best->tint.a * alpha * 255.0f + 0.5f));
        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));
        dl->AddImage(texId, drawMin, drawMax,
                     ImVec2(0, 0), ImVec2(1, 1), tint);
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
        UndoSnapshot();
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
    // for plain ones, ANY painted cell for tilemap entities). Returns
    // the topmost (last-drawn) hit so a child that's painted over a
    // parent wins.
    SceneEditor::EntityId HitTestEntity(const ImVec2& worldMouse) {
        std::vector<SceneEditor::EntityId> ids;
        ids.reserve(64);
        CollectAllEntities(m_scene.Roots(), ids);
        for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
            const auto* e = m_scene.Get(*it);
            if (!e || !e->enabled) continue;
            const ImVec2 wp = EntityWorldPos(*e);
            // Tilemap: hit if the cursor's (gx, gy) maps to a painted
            // cell. Single O(log n) map lookup instead of walking
            // every cell. Falls through to the transform-box hit so a
            // freshly created tilemap with no cells stays clickable
            // via its transform marker.
            if (const auto* tm = e->GetComponent<SceneEditor::TilemapComponent>()) {
                const ResolvedTileset rt = ResolveTileset(*tm);
                if (rt.textureGuid.IsValid() &&
                    rt.cellW > 0 && rt.cellH > 0 &&
                    !tm->cells.empty()) {
                    SceneEditor::TextureImportSettings imp;
                    if (const auto* entry = m_assetDb.FindByGuid(rt.textureGuid)) {
                        imp.FromJson(entry->importSettings);
                    }
                    const float ppu = imp.pixelsPerUnit;
                    if (ppu > 0.0f) {
                        const float cellW = float(rt.cellW) / ppu;
                        const float cellH = float(rt.cellH) / ppu;
                        const int gx = int(std::floor((worldMouse.x - wp.x) / cellW));
                        const int gy = int(std::floor((worldMouse.y - wp.y) / cellH));
                        if (tm->cells.count({gx, gy}) > 0) {
                            return *it;
                        }
                    }
                }
            }
            if (std::abs(worldMouse.x - wp.x) <= 0.5f &&
                std::abs(worldMouse.y - wp.y) <= 0.5f) {
                return *it;
            }
        }
        return SceneEditor::kInvalidEntityId;
    }

    // Tilemap paint mode. Returns true when the input was consumed so
    // the rest of HandleViewportInput (gizmo + selection) is skipped.
    // Cells are addressed by floored (rx, ry) / cellWorld so the user
    // can drag a continuous stroke and every crossed cell ends up
    // painted / erased.
    bool HandleTilemapPaintInput(bool hovered,
                                 const ImVec2& panelMin,
                                 const ImVec2& panelSize) {
        if (m_paintTool == PaintTool::None) return false;
        SceneEditor::Entity* selEntity = m_scene.Get(m_selection);
        if (!selEntity) return false;
        SceneEditor::TilemapComponent* tm =
            selEntity->GetComponent<SceneEditor::TilemapComponent>();
        if (!tm || !tm->tileset.IsValid()) return false;

        // Resolve the tileset reference (Texture or Tileset asset) so
        // the brush snaps to whatever cell size the renderer uses.
        const ResolvedTileset rt = ResolveTileset(*tm);
        if (!rt.textureGuid.IsValid() || rt.cellW <= 0 || rt.cellH <= 0)
            return false;
        SceneEditor::TextureImportSettings imp;
        if (const auto* entry = m_assetDb.FindByGuid(rt.textureGuid)) {
            imp.FromJson(entry->importSettings);
        }
        const float cellWorldW = float(rt.cellW) / imp.pixelsPerUnit;
        const float cellWorldH = float(rt.cellH) / imp.pixelsPerUnit;
        if (cellWorldW <= 0.0f || cellWorldH <= 0.0f) return true;

        const ImVec2 ep   = EntityWorldPos(*selEntity);
        const ImVec2 mp   = ImGui::GetMousePos();
        const ImVec2 wp   = ScreenToWorld(mp, panelMin, panelSize);
        const float  rx   = wp.x - ep.x;
        const float  ry   = wp.y - ep.y;
        const int    gx   = int(std::floor(rx / cellWorldW));
        const int    gy   = int(std::floor(ry / cellWorldH));

        // Ghost overlay. Each tool gets its own colour so the user can
        // tell at a glance which tool is armed. The Rect tool draws an
        // outline across every cell inside the in-progress drag region
        // (instead of just the hovered cell) so the destination shape
        // is visible before release.
        const bool isErase  = (m_paintTool == PaintTool::Eraser);
        const ImU32 ghostFill = isErase
            ? IM_COL32(240, 120, 120,  50)
            : IM_COL32(120, 240, 120,  50);
        const ImU32 ghostLine = isErase
            ? IM_COL32(240, 120, 120, 255)
            : IM_COL32(120, 240, 120, 255);

        const ImVec2 entCenter = WorldToScreen(ep, panelMin, panelSize);
        const float  zoom      = m_viewCam.zoom;
        auto CellScreenRect = [&](int gxc, int gyc, ImVec2& a, ImVec2& b) {
            a = ImVec2{entCenter.x + (gxc       * cellWorldW) * zoom,
                       entCenter.y - ((gyc + 1) * cellWorldH) * zoom};
            b = ImVec2{entCenter.x + ((gxc + 1) * cellWorldW) * zoom,
                       entCenter.y - (gyc       * cellWorldH) * zoom};
        };

        // Brush ghost grows to the full stamp footprint so the user
        // sees exactly which world cells will be written. Rect uses the
        // in-progress drag rectangle; everything else is single-cell.
        const int stampSw = std::abs(m_stampCurCol - m_stampAnchorCol) + 1;
        const int stampSh = std::abs(m_stampCurRow - m_stampAnchorRow) + 1;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hovered) {
            if (m_paintTool == PaintTool::Rect && m_rectActive) {
                const int gx0 = std::min(m_rectAnchorGx, m_rectCurGx);
                const int gx1 = std::max(m_rectAnchorGx, m_rectCurGx);
                const int gy0 = std::min(m_rectAnchorGy, m_rectCurGy);
                const int gy1 = std::max(m_rectAnchorGy, m_rectCurGy);
                ImVec2 a, b, ignored;
                CellScreenRect(gx0, gy0, a, ignored);
                CellScreenRect(gx1, gy1, ignored, b);
                const ImVec2 pMin{std::min(a.x, b.x), std::min(a.y, b.y)};
                const ImVec2 pMax{std::max(a.x, b.x), std::max(a.y, b.y)};
                dl->AddRectFilled(pMin, pMax, ghostFill);
                dl->AddRect(pMin, pMax, ghostLine, 0.0f, 0, 2.0f);
            } else if (m_paintTool == PaintTool::Brush &&
                       (stampSw > 1 || stampSh > 1)) {
                ImVec2 a, ignored1, ignored2, b;
                CellScreenRect(gx,                 gy,                 a,        ignored1);
                CellScreenRect(gx + stampSw - 1,   gy + stampSh - 1,   ignored2, b);
                const ImVec2 pMin{std::min(a.x, b.x), std::min(a.y, b.y)};
                const ImVec2 pMax{std::max(a.x, b.x), std::max(a.y, b.y)};
                dl->AddRectFilled(pMin, pMax, ghostFill);
                dl->AddRect(pMin, pMax, ghostLine, 0.0f, 0, 1.5f);
            } else {
                ImVec2 a, b;
                CellScreenRect(gx, gy, a, b);
                dl->AddRectFilled(a, b, ghostFill);
                dl->AddRect(a, b, ghostLine, 0.0f, 0, 1.5f);
            }
        }

        bool changed = false;
        const bool clicked = hovered &&
                             ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool down    = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        switch (m_paintTool) {
            case PaintTool::Brush: {
                // Snapshot on stroke-start; subsequent drag frames
                // share that single undo entry.
                if (clicked) UndoSnapshot();
                if (hovered && down) changed = StampBrush(*tm, gx, gy);
                break;
            }
            case PaintTool::Eraser: {
                if (clicked) UndoSnapshot();
                if (hovered && down) {
                    if (tm->cells.erase({gx, gy}) > 0) changed = true;
                }
                break;
            }
            case PaintTool::Bucket: {
                if (clicked) {
                    UndoSnapshot();
                    changed = BucketFill(*tm, gx, gy);
                }
                break;
            }
            case PaintTool::Rect: {
                if (clicked) {
                    UndoSnapshot();
                    m_rectActive   = true;
                    m_rectAnchorGx = gx;
                    m_rectAnchorGy = gy;
                    m_rectCurGx    = gx;
                    m_rectCurGy    = gy;
                } else if (down && m_rectActive) {
                    m_rectCurGx = gx;
                    m_rectCurGy = gy;
                }
                if (released && m_rectActive) {
                    changed = FillRect(*tm,
                                       m_rectAnchorGx, m_rectAnchorGy,
                                       m_rectCurGx,    m_rectCurGy);
                    m_rectActive = false;
                }
                break;
            }
            default: break;
        }
        // A drag cancelled by the cursor leaving the viewport should
        // still drop the rect state on release.
        if (released && m_rectActive) {
            m_rectActive = false;
        }

        if (changed) m_scene.MarkDirty();
        if (released) UpdateWindowTitle();
        return true;
    }

    // Flood-fill bounded by "cells with the same tile as (gx, gy)".
    // Returns true if at least one cell was rewritten. Empty source
    // cells short-circuit so a stray click on background doesn't try
    // to fill the unbounded plane.
    bool BucketFill(SceneEditor::TilemapComponent& tm, int gx, int gy) {
        auto src = tm.cells.find({gx, gy});
        if (src == tm.cells.end()) return false;
        const int srcTile = src->second;
        if (srcTile == m_paintTileIndex) return false;
        std::vector<std::pair<int, int>> stack;
        stack.push_back({gx, gy});
        bool changed = false;
        while (!stack.empty()) {
            const auto pos = stack.back();
            stack.pop_back();
            auto it = tm.cells.find(pos);
            if (it == tm.cells.end() || it->second != srcTile) continue;
            it->second = m_paintTileIndex;
            changed = true;
            stack.push_back({pos.first - 1, pos.second});
            stack.push_back({pos.first + 1, pos.second});
            stack.push_back({pos.first,     pos.second - 1});
            stack.push_back({pos.first,     pos.second + 1});
        }
        return changed;
    }

    // Stamp dimensions + source-tile lookup. Palette Y is top-down
    // (row 0 = top of source texture); world Y is up so the bottom row
    // of the stamp is the palette's bottom row.
    void StampBounds(int& c0, int& r0, int& c1, int& r1) const {
        c0 = std::min(m_stampAnchorCol, m_stampCurCol);
        r0 = std::min(m_stampAnchorRow, m_stampCurRow);
        c1 = std::max(m_stampAnchorCol, m_stampCurCol);
        r1 = std::max(m_stampAnchorRow, m_stampCurRow);
    }

    int TexCols(const SceneEditor::TilemapComponent& tm) {
        const ResolvedTileset rt = ResolveTileset(tm);
        if (rt.cellW <= 0) return 0;
        NaraGame::RHITexture* tex =
            m_assetDb.GetTexture(GetDevice(), rt.textureGuid);
        if (!tex) return 0;
        return int(tex->Width()) / rt.cellW;
    }

    // Place the stamp with its bottom-left at (gx, gy). dxWorld grows
    // right, dyWorld grows up; the palette row that ends up at dyWorld
    // is (sh - 1 - dyWorld) so the visual top of the palette becomes
    // the visual top of the painted region.
    bool StampBrush(SceneEditor::TilemapComponent& tm, int gx, int gy) {
        // Autotile branch: place a single autotile cell and recompute
        // masks for the cell + 4 cardinal neighbours so the seam
        // pattern stays consistent with the painted region.
        if (m_activeAutotile.IsValid()) {
            if (SceneEditor::AutotileAsset* at =
                    m_assetDb.GetAutotile(m_activeAutotile)) {
                return PaintAutotileCell(tm, gx, gy, *at);
            }
        }
        const int texCols = TexCols(tm);
        if (texCols <= 0) return false;
        int c0, r0, c1, r1;
        StampBounds(c0, r0, c1, r1);
        const int sw = c1 - c0 + 1;
        const int sh = r1 - r0 + 1;
        bool changed = false;
        for (int dyWorld = 0; dyWorld < sh; ++dyWorld) {
            const int srcRow = r0 + (sh - 1 - dyWorld);
            for (int dx = 0; dx < sw; ++dx) {
                const int tile = srcRow * texCols + (c0 + dx);
                const std::pair<int,int> key{gx + dx, gy + dyWorld};
                auto it = tm.cells.find(key);
                if (it == tm.cells.end() || it->second != tile) {
                    tm.cells[key] = tile;
                    changed = true;
                }
            }
        }
        return changed;
    }

    // Fill the inclusive rect (ax, ay)-(bx, by) by tiling the stamp.
    // Anchor cell (x0, y0) gets the stamp's bottom-left tile so the
    // pattern reads naturally from the anchor outward.
    bool FillRect(SceneEditor::TilemapComponent& tm,
                  int ax, int ay, int bx, int by) {
        // Autotile branch: stamp every cell with the autotile's
        // standalone tile (mask 0) first, then recompute every cell +
        // its neighbours so the seam pattern reaches across the rect's
        // entire border.
        if (m_activeAutotile.IsValid()) {
            if (SceneEditor::AutotileAsset* at =
                    m_assetDb.GetAutotile(m_activeAutotile)) {
                return PaintAutotileRect(tm, ax, ay, bx, by, *at);
            }
        }
        const int texCols = TexCols(tm);
        if (texCols <= 0) return false;
        const int x0 = std::min(ax, bx);
        const int x1 = std::max(ax, bx);
        const int y0 = std::min(ay, by);
        const int y1 = std::max(ay, by);
        int c0, r0, c1, r1;
        StampBounds(c0, r0, c1, r1);
        const int sw = c1 - c0 + 1;
        const int sh = r1 - r0 + 1;
        bool changed = false;
        for (int yy = y0; yy <= y1; ++yy) {
            for (int xx = x0; xx <= x1; ++xx) {
                const int dx       = ((xx - x0) % sw + sw) % sw;
                const int dyWorld  = ((yy - y0) % sh + sh) % sh;
                const int srcRow   = r0 + (sh - 1 - dyWorld);
                const int tile     = srcRow * texCols + (c0 + dx);
                auto it = tm.cells.find({xx, yy});
                if (it == tm.cells.end() || it->second != tile) {
                    tm.cells[{xx, yy}] = tile;
                    changed = true;
                }
            }
        }
        return changed;
    }

    // Read the mask for (gx, gy) by looking at neighbours. Cardinal
    // mode tracks 4 cardinal neighbours; Blob mode tracks all 8 then
    // canonicalises (a diagonal only counts when its two adjacent
    // cardinals are also filled).
    int ComputeAutotileMask(const SceneEditor::TilemapComponent& tm,
                            int gx, int gy,
                            const SceneEditor::AutotileAsset& at) const {
        auto matches = [&](int x, int y) {
            auto it = tm.cells.find({x, y});
            return it != tm.cells.end() &&
                   at.tileSet.find(it->second) != at.tileSet.end();
        };
        if (at.mode == SceneEditor::AutotileAsset::Mode::Blob) {
            int m = 0;
            // World Y up: NE = (gx+1, gy+1), etc.
            if (matches(gx,     gy + 1)) m |= 1;    // N
            if (matches(gx + 1, gy + 1)) m |= 2;    // NE
            if (matches(gx + 1, gy    )) m |= 4;    // E
            if (matches(gx + 1, gy - 1)) m |= 8;    // SE
            if (matches(gx,     gy - 1)) m |= 16;   // S
            if (matches(gx - 1, gy - 1)) m |= 32;   // SW
            if (matches(gx - 1, gy    )) m |= 64;   // W
            if (matches(gx - 1, gy + 1)) m |= 128;  // NW
            return SceneEditor::AutotileAsset::Canonicalize8bit(m);
        }
        int m = 0;
        if (matches(gx,     gy + 1)) m |= 1;   // N (world Y up)
        if (matches(gx + 1, gy    )) m |= 2;   // E
        if (matches(gx,     gy - 1)) m |= 4;   // S
        if (matches(gx - 1, gy    )) m |= 8;   // W
        return m;
    }

    // Refresh the tile at (gx, gy) based on its neighbours. Only
    // touches cells that are already part of the autotile group so
    // non-autotile content next to the brush keeps its visual.
    bool RefreshAutotileCell(SceneEditor::TilemapComponent& tm,
                             int gx, int gy,
                             const SceneEditor::AutotileAsset& at) {
        auto it = tm.cells.find({gx, gy});
        if (it == tm.cells.end()) return false;
        if (at.tileSet.find(it->second) == at.tileSet.end()) return false;
        const int m = ComputeAutotileMask(tm, gx, gy, at);
        const int newTile =
            (at.mode == SceneEditor::AutotileAsset::Mode::Blob)
            ? at.blobMask[size_t(m & 0xFF)]
            : at.mask[size_t(m & 15)];
        if (it->second == newTile) return false;
        it->second = newTile;
        return true;
    }

    // Placeholder tile for "this cell now belongs to the autotile
    // group". Cardinal -> mask[0], Blob -> blobMask[0]. Both are the
    // standalone (no-neighbour) tile in their respective modes.
    int AutotileStandalone(const SceneEditor::AutotileAsset& at) const {
        return (at.mode == SceneEditor::AutotileAsset::Mode::Blob)
            ? at.blobMask[0] : at.mask[0];
    }

    bool PaintAutotileCell(SceneEditor::TilemapComponent& tm,
                           int gx, int gy,
                           const SceneEditor::AutotileAsset& at) {
        // Drop a placeholder tile so RefreshAutotileCell recognises
        // the cell as autotile, then refresh self + (4 or 8) neighbours.
        bool changed = false;
        auto it = tm.cells.find({gx, gy});
        if (it == tm.cells.end() ||
            at.tileSet.find(it->second) == at.tileSet.end()) {
            tm.cells[{gx, gy}] = AutotileStandalone(at);
            changed = true;
        }
        const bool blob = (at.mode == SceneEditor::AutotileAsset::Mode::Blob);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (!blob && dx != 0 && dy != 0) continue;
                changed |= RefreshAutotileCell(tm, gx + dx, gy + dy, at);
            }
        }
        return changed;
    }

    bool PaintAutotileRect(SceneEditor::TilemapComponent& tm,
                           int ax, int ay, int bx, int by,
                           const SceneEditor::AutotileAsset& at) {
        const int x0 = std::min(ax, bx);
        const int x1 = std::max(ax, bx);
        const int y0 = std::min(ay, by);
        const int y1 = std::max(ay, by);
        const int standalone = AutotileStandalone(at);
        bool changed = false;
        for (int yy = y0; yy <= y1; ++yy) {
            for (int xx = x0; xx <= x1; ++xx) {
                auto it = tm.cells.find({xx, yy});
                if (it == tm.cells.end() ||
                    at.tileSet.find(it->second) == at.tileSet.end()) {
                    tm.cells[{xx, yy}] = standalone;
                    changed = true;
                }
            }
        }
        // Recompute everything in the rect + a 1-cell apron so the
        // border seams against existing autotile content.
        for (int yy = y0 - 1; yy <= y1 + 1; ++yy) {
            for (int xx = x0 - 1; xx <= x1 + 1; ++xx) {
                changed |= RefreshAutotileCell(tm, xx, yy, at);
            }
        }
        return changed;
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

        // Tilemap paint dispatch. When the selected entity has a
        // Tilemap component AND a paint tool is active, the left mouse
        // belongs to painting; the gizmo + selection flow below is
        // skipped. Middle-drag pan + wheel zoom still work above.
        if (HandleTilemapPaintInput(hovered, panelMin, panelSize)) {
            return;
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
        // Snapshot once per drag (mouse-down). Every drag frame mutates
        // through the same undo entry.
        UndoSnapshot();
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

        // Unified sort over every visible component. A single entity
        // with both a SpriteRenderer and a Tilemap shows up twice with
        // independent sort keys, so the user can layer them however
        // they want. Markers are editor-only visuals -- they're not in
        // the sorted list and draw after everything so they stay
        // clickable.
        enum class Kind { Sprite, Tilemap };
        struct SortKey {
            Kind   kind;
            int    layerPrio;
            int    order;
            size_t traversal;
            SceneEditor::EntityId id;
        };
        std::vector<SortKey> drawList;
        std::vector<SceneEditor::EntityId> markers;
        drawList.reserve(ids.size() * 2);
        for (size_t i = 0; i < ids.size(); ++i) {
            const auto* e = m_scene.Get(ids[i]);
            if (!e || !e->enabled) continue;
            const auto* tm  = e->GetComponent<SceneEditor::TilemapComponent>();
            const auto* spr = e->GetComponent<SceneEditor::SpriteRendererComponent>();
            const bool hasTm  = tm  && tm->tileset.IsValid();
            const bool hasSpr = spr && spr->sprite.IsValid();
            if (hasTm) {
                drawList.push_back({
                    Kind::Tilemap,
                    m_project.LayerPriority(tm->sortingLayer),
                    tm->orderInLayer, i, ids[i]});
            }
            if (hasSpr) {
                drawList.push_back({
                    Kind::Sprite,
                    m_project.LayerPriority(spr->sortingLayer),
                    spr->orderInLayer, i, ids[i]});
            }
            if (!hasTm && !hasSpr) markers.push_back(ids[i]);
        }
        std::sort(drawList.begin(), drawList.end(),
                  [](const SortKey& a, const SortKey& b) {
                      if (a.layerPrio != b.layerPrio) return a.layerPrio < b.layerPrio;
                      if (a.order     != b.order)     return a.order     < b.order;
                      return a.traversal < b.traversal;
                  });

        for (const SortKey& k : drawList) {
            const auto* e = m_scene.Get(k.id);
            if (!e) continue;
            const ImVec2 wp = EntityWorldPos(*e);
            const ImVec2 c  = WorldToScreen(wp, panelMin, panelSize);
            const bool sel = (k.id == m_selection);
            if (k.kind == Kind::Tilemap) {
                if (const auto* tm =
                        e->GetComponent<SceneEditor::TilemapComponent>()) {
                    DrawTilemapEntity(dl, *e, *tm, panelMin, panelSize, sel);
                }
            } else {
                if (const auto* spr =
                        e->GetComponent<SceneEditor::SpriteRendererComponent>()) {
                    DrawSpriteEntity(dl, *e, *spr, c, sel);
                }
            }
        }
        // Marker entities are editor-only -- in standalone they
        // would just litter the screen with debug glyphs.
        if (!m_standalone) {
            for (SceneEditor::EntityId id : markers) {
                const auto* e = m_scene.Get(id);
                if (!e) continue;
                const ImVec2 wp = EntityWorldPos(*e);
                const ImVec2 c  = WorldToScreen(wp, panelMin, panelSize);
                DrawMarkerEntity(dl, *e, c, id == m_selection);
            }
        }

        // M9c2 particle systems. Each live particle is an
        // axis-aligned coloured quad sized by `size` (world
        // units) and tinted by lerping startColor -> endColor
        // across age/lifetime. Drawn after sprites/tilemaps
        // so smoke / sparks naturally sit on top of the scene.
        // M9c4: when the component carries a valid texture
        // GUID, route through AddImageQuad with the whole
        // texture mapped onto each particle (premultiplied by
        // the lerped tint) so an alpha PNG of a smoke puff
        // gives a real soft particle. The untextured
        // AddRectFilled fallback stays the default for the
        // common "spark" / "ember" sharp-pixel look.
        for (SceneEditor::EntityId id : ids) {
            const auto* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            const auto* ps =
                e->GetComponent<SceneEditor::ParticleSystemComponent>();
            if (!ps || ps->_liveCount <= 0) continue;
            NaraGame::RHITexture* tex = nullptr;
            ImTextureID texId = (ImTextureID)0;
            // M9c5/M9c6 per-emitter UV resolve. Frame cache
            // holds one UV rect per animation frame; an
            // emitter without animFrames stays one rect (the
            // M9c5 path). Whole-texture default + orphan
            // fallback keep the renderer safe against stale
            // sub-sprite names.
            struct UvRect { ImVec2 a, b, c, d; };
            const UvRect kFullUv = {
                {0.0f, 0.0f}, {1.0f, 0.0f},
                {1.0f, 1.0f}, {0.0f, 1.0f}
            };
            std::vector<UvRect> frameUVs;
            if (ps->texture.IsValid()) {
                tex = m_assetDb.GetTexture(GetDevice(), ps->texture);
                if (tex) {
                    texId = static_cast<ImTextureID>(
                        reinterpret_cast<uintptr_t>(
                            tex->GetNativeView()));
                    const SceneEditor::AssetEntry* en =
                        m_assetDb.FindByGuid(ps->texture);
                    SceneEditor::TextureImportSettings imp;
                    if (en) imp.FromJson(en->importSettings);
                    const float tw = float(tex->Width());
                    const float th = float(tex->Height());
                    auto resolveName =
                        [&](const std::string& name) -> UvRect {
                        if (name.empty() || tw <= 0.0f || th <= 0.0f) {
                            return kFullUv;
                        }
                        for (const SceneEditor::SubSprite& s :
                                imp.spriteSheet.sprites) {
                            if (s.name != name) continue;
                            const float u0 = float(s.x) / tw;
                            const float v0 = float(s.y) / th;
                            const float u1 = float(s.x + s.w) / tw;
                            const float v1 = float(s.y + s.h) / th;
                            return UvRect{
                                {u0, v0}, {u1, v0},
                                {u1, v1}, {u0, v1}};
                        }
                        return kFullUv;
                    };
                    if (!ps->animFrames.empty()) {
                        frameUVs.reserve(ps->animFrames.size());
                        for (const std::string& n : ps->animFrames) {
                            frameUVs.push_back(resolveName(n));
                        }
                    } else {
                        frameUVs.push_back(resolveName(ps->subSprite));
                    }
                }
            }
            if (frameUVs.empty()) frameUVs.push_back(kFullUv);
            const bool animated =
                !ps->animFrames.empty() && frameUVs.size() > 1;
            const float fps =
                (ps->animFps > 0.0f) ? ps->animFps : 0.0f;
            const int   frameCount = int(frameUVs.size());
            for (int i = 0; i < ps->_liveCount; ++i) {
                const auto& p = ps->_pool[size_t(i)];
                const float t = (p.lifetime > 0.0f)
                    ? (p.age / p.lifetime) : 1.0f;
                const float r = ps->startColor.r +
                    (ps->endColor.r - ps->startColor.r) * t;
                const float g = ps->startColor.g +
                    (ps->endColor.g - ps->startColor.g) * t;
                const float b = ps->startColor.b +
                    (ps->endColor.b - ps->startColor.b) * t;
                const float a = ps->startColor.a +
                    (ps->endColor.a - ps->startColor.a) * t;
                const float clA = (a < 0.0f) ? 0.0f :
                                  ((a > 1.0f) ? 1.0f : a);
                if (clA <= 0.001f) continue;
                const ImVec2 sc = WorldToScreen(
                    ImVec2{p.pos.x, p.pos.y}, panelMin, panelSize);
                // M9c8 size lerp: startSize at age 0, endSize
                // at age = lifetime. Same `t` the colour lerp
                // already used, just re-applied to the radius.
                const float curSize =
                    p.size + (p.endSize - p.size) * t;
                const float drawSize = curSize < 0.0f ? 0.0f : curSize;
                const float half = drawSize * 0.5f * m_viewCam.zoom;
                const ImU32 col = IM_COL32(
                    int(r * 255.0f),
                    int(g * 255.0f),
                    int(b * 255.0f),
                    int(clA * 255.0f));
                // M9c7 rotation: project the 4 corners
                // through cos/sin around the particle centre.
                // Velocity-align overrides the stored angle
                // with atan2(vy, vx) -- screen-Y points down
                // so we flip the y component to keep the
                // sprite's "forward" facing world-up when vy
                // is positive.
                float rotRad = p.rotation;
                if (ps->velocityAlign) {
                    rotRad = std::atan2(p.vel.y, p.vel.x);
                }
                const float cR = std::cos(rotRad);
                const float sR = std::sin(rotRad);
                // World-space local corners. Y-flip for the
                // screen happens inside the projection so
                // rotation direction stays CCW in world space.
                auto Project = [&](float lx, float ly) {
                    const float rx = cR * lx - sR * ly;
                    const float ry = sR * lx + cR * ly;
                    return ImVec2{sc.x + rx, sc.y - ry};
                };
                const ImVec2 pTL = Project(-half,  half);
                const ImVec2 pTR = Project( half,  half);
                const ImVec2 pBR = Project( half, -half);
                const ImVec2 pBL = Project(-half, -half);
                if (tex) {
                    // M9c6 per-particle frame select. Each
                    // particle's age maps to a frame index
                    // independently, so two particles spawned
                    // a fraction of a second apart cycle out
                    // of phase. Loop wraps via modulo; non-
                    // loop clamps at the last frame.
                    int frameIdx = 0;
                    if (animated && fps > 0.0f) {
                        const int raw = int(p.age * fps);
                        if (ps->animLoop) {
                            frameIdx = ((raw % frameCount) + frameCount)
                                       % frameCount;
                        } else {
                            frameIdx = raw < 0 ? 0 :
                                (raw >= frameCount ? frameCount - 1
                                                   : raw);
                        }
                    }
                    const UvRect& uv = frameUVs[size_t(frameIdx)];
                    dl->AddImageQuad(texId,
                        pTL, pTR, pBR, pBL,
                        uv.a, uv.b, uv.c, uv.d, col);
                } else {
                    // Untextured rect path picks up the same
                    // rotation through AddQuadFilled. When
                    // rotation is 0 the result is identical to
                    // the M9c2 AddRectFilled output.
                    dl->AddQuadFilled(pTL, pTR, pBR, pBL, col);
                }
            }
        }

        // M9c11 camera frustum gizmo. Editor-only -- shows the
        // rectangle the active CameraComponent would render
        // to a 16:9 viewport at its `zoom`. Lets the user size
        // a camera by eye + spot which entities sit outside the
        // current shot. Suppressed in standalone (the camera
        // *is* the view, no need to overlay it on itself).
        if (!m_standalone) {
            for (SceneEditor::EntityId id : ids) {
                const auto* e = m_scene.Get(id);
                if (!e || !e->enabled) continue;
                const auto* cm = e->GetComponent<
                    SceneEditor::CameraComponent>();
                const auto* tr = e->GetComponent<
                    SceneEditor::TransformComponent>();
                if (!cm || !tr || cm->zoom <= 0.0f) continue;
                // Frustum half-extents (world units) at 16:9
                // for a 1600x900 window the editor defaults
                // to. The actual standalone view follows the
                // window's aspect ratio at run time; the
                // editor gizmo just gives a representative
                // size + scales with the camera's zoom.
                const float halfW = (1600.0f / cm->zoom) * 0.5f;
                const float halfH = ( 900.0f / cm->zoom) * 0.5f;
                const ImVec2 tl = WorldToScreen(
                    ImVec2{tr->position.x - halfW,
                           tr->position.y + halfH},
                    panelMin, panelSize);
                const ImVec2 br = WorldToScreen(
                    ImVec2{tr->position.x + halfW,
                           tr->position.y - halfH},
                    panelMin, panelSize);
                const ImU32 col = IM_COL32(255, 200, 80, 200);
                dl->AddRect(tl, br, col, 0.0f, 0, 2.0f);
                // Small "CAM" label so the user can tell a
                // camera frustum from an emission-shape box.
                dl->AddText(
                    ImVec2{tl.x + 4.0f, tl.y + 2.0f},
                    col, "CAM");
            }
        }

        // M9c10 emission-shape gizmo. Editor-only -- shows the
        // outline of where new particles will spawn so the user
        // can size a circle / box / line by eye. Skipped in
        // standalone (game runtime) so a finished game doesn't
        // leak debug glyphs.
        if (!m_standalone) {
            for (SceneEditor::EntityId id : ids) {
                const auto* e = m_scene.Get(id);
                if (!e || !e->enabled) continue;
                const auto* ps =
                    e->GetComponent<
                        SceneEditor::ParticleSystemComponent>();
                const auto* tr =
                    e->GetComponent<SceneEditor::TransformComponent>();
                if (!ps || !tr) continue;
                using Sh = SceneEditor::ParticleEmissionShape;
                if (ps->shape == Sh::Point) continue;
                const ImVec2 sc = WorldToScreen(
                    ImVec2{tr->position.x, tr->position.y},
                    panelMin, panelSize);
                const ImU32 gz = IM_COL32(150, 220, 255, 180);
                const float z = m_viewCam.zoom;
                if (ps->shape == Sh::CircleEdge ||
                    ps->shape == Sh::CircleVolume) {
                    dl->AddCircle(sc, ps->shapeRadius * z,
                                  gz, 48, 1.5f);
                } else if (ps->shape == Sh::BoxEdge ||
                            ps->shape == Sh::BoxVolume) {
                    const float hx = ps->shapeBoxSize.x * 0.5f * z;
                    const float hy = ps->shapeBoxSize.y * 0.5f * z;
                    dl->AddRect(
                        ImVec2{sc.x - hx, sc.y - hy},
                        ImVec2{sc.x + hx, sc.y + hy},
                        gz, 0.0f, 0, 1.5f);
                } else if (ps->shape == Sh::Line) {
                    const ImVec2 end =
                        WorldToScreen(
                            ImVec2{
                                tr->position.x +
                                    ps->shapeLineEnd.x,
                                tr->position.y +
                                    ps->shapeLineEnd.y},
                            panelMin, panelSize);
                    dl->AddLine(sc, end, gz, 1.5f);
                }
            }
        }

        // Collider gizmos -- box outlines + circle outlines.
        // Colour gates on the M7c1 overlap state:
        //   yellow  = currently overlapping another collider
        //   green   = no contact
        //   selected entity gets thicker stroke + brighter alpha
        // Triggers render dashed (alternating stippled segments
        // would be ideal but ImGui DrawList doesn't ship one --
        // we use AddRect with a thinner outline + outline-only
        // fill instead so triggers read as "boundary" + solids
        // (future) can render with a filled wash).
        for (SceneEditor::EntityId id : ids) {
            const auto* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            const auto* tr =
                e->GetComponent<SceneEditor::TransformComponent>();
            const auto* bc =
                e->GetComponent<SceneEditor::BoxColliderComponent>();
            if (!tr || !bc) continue;
            const float halfX = bc->size.x * 0.5f * tr->scale.x;
            const float halfY = bc->size.y * 0.5f * tr->scale.y;
            const float cx = tr->position.x + bc->offset.x;
            const float cy = tr->position.y + bc->offset.y;
            // M7c13: gizmo follows the same rotation the
            // runtime collider sees, so what you see really is
            // the SAT footprint.
            const float rotRad = tr->rotation
                * 3.14159265358979323846f / 180.0f;
            const float cosR = std::cos(rotRad);
            const float sinR = std::sin(rotRad);
            auto Corner = [&](float lx, float ly) {
                const float wx = cx + cosR * lx - sinR * ly;
                const float wy = cy + sinR * lx + cosR * ly;
                return WorldToScreen(ImVec2(wx, wy),
                                     panelMin, panelSize);
            };
            const ImVec2 pTL = Corner(-halfX,  halfY);
            const ImVec2 pTR = Corner( halfX,  halfY);
            const ImVec2 pBR = Corner( halfX, -halfY);
            const ImVec2 pBL = Corner(-halfX, -halfY);
            bool overlapping = false;
            for (const auto& key : m_collisionPairs) {
                if (key.first == id || key.second == id) {
                    overlapping = true; break;
                }
            }
            const bool sel = (id == m_selection);
            const ImU32 col = overlapping
                ? (sel ? IM_COL32(255, 220, 80, 255)
                       : IM_COL32(255, 200, 60, 200))
                : (sel ? IM_COL32(120, 255, 140, 255)
                       : IM_COL32(80,  220, 100, 180));
            const float thickness = sel ? 2.5f : 1.5f;
            const ImVec2 pts[5] = { pTL, pTR, pBR, pBL, pTL };
            dl->AddPolyline(pts, 5, col, ImDrawFlags_None, thickness);
            // Centre tick to mark the offset origin.
            const ImVec2 origin = WorldToScreen(
                ImVec2(cx, cy), panelMin, panelSize);
            dl->AddCircleFilled(origin, 2.5f, col);
        }
        // CircleCollider gizmos: same colour rules + per-screen-
        // pixel radius computed from the camera zoom so the
        // outline stays accurate when the user zooms.
        for (SceneEditor::EntityId id : ids) {
            const auto* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            const auto* tr =
                e->GetComponent<SceneEditor::TransformComponent>();
            const auto* cc =
                e->GetComponent<SceneEditor::CircleColliderComponent>();
            if (!tr || !cc) continue;
            const float scaleAvg = (tr->scale.x + tr->scale.y) * 0.5f;
            const float wr  = cc->radius * scaleAvg;
            const float cx  = tr->position.x + cc->offset.x;
            const float cy  = tr->position.y + cc->offset.y;
            const ImVec2 cs = WorldToScreen(
                ImVec2(cx, cy), panelMin, panelSize);
            // Use a pair of cardinal world points to back out the
            // current zoom-aware pixel radius (avoids leaking the
            // viewport camera state into this scope).
            const ImVec2 rs = WorldToScreen(
                ImVec2(cx + wr, cy), panelMin, panelSize);
            const float pixelR = rs.x - cs.x;
            bool overlapping = false;
            for (const auto& key : m_collisionPairs) {
                if (key.first == id || key.second == id) {
                    overlapping = true; break;
                }
            }
            const bool sel = (id == m_selection);
            const ImU32 col = overlapping
                ? (sel ? IM_COL32(255, 220, 80, 255)
                       : IM_COL32(255, 200, 60, 200))
                : (sel ? IM_COL32(120, 255, 140, 255)
                       : IM_COL32(80,  220, 100, 180));
            const float thickness = sel ? 2.5f : 1.5f;
            dl->AddCircle(cs, pixelR, col, 32, thickness);
            dl->AddCircleFilled(cs, 2.5f, col);
        }
        // M7c32: EdgeCollider gizmos. Open chain renders as
        // a polyline; closed chain adds the wrap segment.
        for (SceneEditor::EntityId id : ids) {
            const auto* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            const auto* tr =
                e->GetComponent<SceneEditor::TransformComponent>();
            const auto* ec =
                e->GetComponent<SceneEditor::EdgeColliderComponent>();
            if (!tr || !ec) continue;
            const int n = int(ec->vertices.size());
            if (n < 2) continue;
            const float rotRad = tr->rotation
                * 3.14159265358979323846f / 180.0f;
            const float cosR = std::cos(rotRad);
            const float sinR = std::sin(rotRad);
            const float ox = tr->position.x + ec->offset.x;
            const float oy = tr->position.y + ec->offset.y;
            const float sx = tr->scale.x;
            const float sy = tr->scale.y;
            bool overlapping = false;
            for (const auto& key : m_collisionPairs) {
                if (key.first == id || key.second == id) {
                    overlapping = true; break;
                }
            }
            const bool sel = (id == m_selection);
            const ImU32 col = overlapping
                ? (sel ? IM_COL32(255, 220, 80, 255)
                       : IM_COL32(255, 200, 60, 200))
                : (sel ? IM_COL32(120, 255, 140, 255)
                       : IM_COL32(80,  220, 100, 180));
            const float thickness = sel ? 2.5f : 1.5f;
            std::vector<ImVec2> screen;
            screen.reserve(n + 1);
            for (int i = 0; i < n; ++i) {
                const float lvx = ec->vertices[i].x * sx;
                const float lvy = ec->vertices[i].y * sy;
                const float wx = ox + cosR * lvx - sinR * lvy;
                const float wy = oy + sinR * lvx + cosR * lvy;
                screen.push_back(WorldToScreen(
                    ImVec2(wx, wy), panelMin, panelSize));
            }
            const ImDrawFlags flags = ec->closed
                ? ImDrawFlags_Closed : ImDrawFlags_None;
            dl->AddPolyline(screen.data(), n, col,
                             flags, thickness);
            // Vertex dots so the user can see where each
            // editable handle sits.
            for (int i = 0; i < n; ++i) {
                dl->AddCircleFilled(screen[i], 3.0f, col);
            }
        }

        // M7c28: PolygonCollider gizmos. Trace the convex
        // outline as a closed polyline through world->screen.
        for (SceneEditor::EntityId id : ids) {
            const auto* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            const auto* tr =
                e->GetComponent<SceneEditor::TransformComponent>();
            const auto* pgc =
                e->GetComponent<SceneEditor::PolygonColliderComponent>();
            if (!tr || !pgc) continue;
            const int n = int(pgc->vertices.size());
            if (n < 3) continue;
            const float rotRad = tr->rotation
                * 3.14159265358979323846f / 180.0f;
            const float cosR = std::cos(rotRad);
            const float sinR = std::sin(rotRad);
            const float ox = tr->position.x + pgc->offset.x;
            const float oy = tr->position.y + pgc->offset.y;
            const float sx = tr->scale.x;
            const float sy = tr->scale.y;
            bool overlapping = false;
            for (const auto& key : m_collisionPairs) {
                if (key.first == id || key.second == id) {
                    overlapping = true; break;
                }
            }
            const bool sel = (id == m_selection);
            const ImU32 col = overlapping
                ? (sel ? IM_COL32(255, 220, 80, 255)
                       : IM_COL32(255, 200, 60, 200))
                : (sel ? IM_COL32(120, 255, 140, 255)
                       : IM_COL32(80,  220, 100, 180));
            const float thickness = sel ? 2.5f : 1.5f;
            std::vector<ImVec2> screen;
            screen.reserve(n + 1);
            for (int i = 0; i < n; ++i) {
                const float lvx = pgc->vertices[i].x * sx;
                const float lvy = pgc->vertices[i].y * sy;
                const float wx = ox + cosR * lvx - sinR * lvy;
                const float wy = oy + sinR * lvx + cosR * lvy;
                screen.push_back(WorldToScreen(
                    ImVec2(wx, wy), panelMin, panelSize));
            }
            dl->AddPolyline(screen.data(), n, col,
                             ImDrawFlags_Closed, thickness);
            const ImVec2 origin = WorldToScreen(
                ImVec2(ox, oy), panelMin, panelSize);
            dl->AddCircleFilled(origin, 2.5f, col);
        }
        // M7c27: CapsuleCollider gizmos. Outline = rotated
        // rectangle through the spine (2r wide, 2(halfLen+r)
        // tall) trimmed with two semicircular caps. Trace the
        // two side segments + two arcs as polylines so the
        // shape matches the runtime decomposition exactly.
        for (SceneEditor::EntityId id : ids) {
            const auto* e = m_scene.Get(id);
            if (!e || !e->enabled) continue;
            const auto* tr =
                e->GetComponent<SceneEditor::TransformComponent>();
            const auto* cap =
                e->GetComponent<SceneEditor::CapsuleColliderComponent>();
            if (!tr || !cap) continue;
            const float scaleX = tr->scale.x;
            const float scaleY = tr->scale.y;
            const float scaleAvg = (scaleX + scaleY) * 0.5f;
            const float wr  = cap->radius     * scaleAvg;
            const float whl = cap->halfLength * scaleY;
            const float cx  = tr->position.x + cap->offset.x;
            const float cy  = tr->position.y + cap->offset.y;
            const float rotRad = tr->rotation
                * 3.14159265358979323846f / 180.0f;
            const float cosR = std::cos(rotRad);
            const float sinR = std::sin(rotRad);
            // Spine endpoints in world (local Y is the spine).
            const float dirX = -sinR;
            const float dirY =  cosR;
            const float pAx = cx - dirX * whl;
            const float pAy = cy - dirY * whl;
            const float pBx = cx + dirX * whl;
            const float pBy = cy + dirY * whl;
            // Right-perpendicular vector of magnitude r.
            const float rx =  cosR * wr;
            const float ry =  sinR * wr;
            bool overlapping = false;
            for (const auto& key : m_collisionPairs) {
                if (key.first == id || key.second == id) {
                    overlapping = true; break;
                }
            }
            const bool sel = (id == m_selection);
            const ImU32 col = overlapping
                ? (sel ? IM_COL32(255, 220, 80, 255)
                       : IM_COL32(255, 200, 60, 200))
                : (sel ? IM_COL32(120, 255, 140, 255)
                       : IM_COL32(80,  220, 100, 180));
            const float thickness = sel ? 2.5f : 1.5f;
            // Side line 1: pA + r-perp -> pB + r-perp.
            const ImVec2 s1a = WorldToScreen(
                ImVec2(pAx + rx, pAy + ry), panelMin, panelSize);
            const ImVec2 s1b = WorldToScreen(
                ImVec2(pBx + rx, pBy + ry), panelMin, panelSize);
            dl->AddLine(s1a, s1b, col, thickness);
            const ImVec2 s2a = WorldToScreen(
                ImVec2(pAx - rx, pAy - ry), panelMin, panelSize);
            const ImVec2 s2b = WorldToScreen(
                ImVec2(pBx - rx, pBy - ry), panelMin, panelSize);
            dl->AddLine(s2a, s2b, col, thickness);
            // Two semicircular caps. Approximate each with N
            // segments rendered as a polyline through world-
            // space points so the camera zoom respects the
            // capsule's radius automatically.
            const int N = 16;
            ImVec2 capA[N + 1];
            ImVec2 capB[N + 1];
            for (int i = 0; i <= N; ++i) {
                const float th = float(i) / float(N) * 3.14159265f;
                const float lx = std::sin(th) * wr;
                const float ly = std::cos(th) * wr;
                // pA cap: rotate (lx, -ly) into world + add pA.
                const float wxA = pAx + cosR * lx - sinR * (-ly);
                const float wyA = pAy + sinR * lx + cosR * (-ly);
                capA[i] = WorldToScreen(
                    ImVec2(wxA, wyA), panelMin, panelSize);
                // pB cap: rotate (-lx, ly) into world + add pB.
                const float wxB = pBx + cosR * (-lx) - sinR * ly;
                const float wyB = pBy + sinR * (-lx) + cosR * ly;
                capB[i] = WorldToScreen(
                    ImVec2(wxB, wyB), panelMin, panelSize);
            }
            dl->AddPolyline(capA, N + 1, col,
                             ImDrawFlags_None, thickness);
            dl->AddPolyline(capB, N + 1, col,
                             ImDrawFlags_None, thickness);
            const ImVec2 origin = WorldToScreen(
                ImVec2(cx, cy), panelMin, panelSize);
            dl->AddCircleFilled(origin, 2.5f, col);
        }
    }

    // Resolve a TilemapComponent's "tileset" GUID into the renderer's
    // working values. The GUID can point at a Texture (the M4c1 path,
    // cellSize comes from the component) or a .ntileset asset (cellSize
    // comes from the tileset). On a Tileset, the source texture's GUID
    // is loaded out as the underlying texture for sampling.
    struct ResolvedTileset {
        SceneEditor::AssetGuid textureGuid;
        int                    cellW = 0;
        int                    cellH = 0;
    };

    ResolvedTileset ResolveTileset(const SceneEditor::TilemapComponent& tm) {
        ResolvedTileset r;
        r.textureGuid = tm.tileset;
        r.cellW       = tm.cellPixelW;
        r.cellH       = tm.cellPixelH;
        if (const auto* entry = m_assetDb.FindByGuid(tm.tileset)) {
            if (entry->typeName == "Tileset") {
                if (SceneEditor::TilesetAsset* ts =
                        m_assetDb.GetTileset(tm.tileset)) {
                    r.textureGuid = ts->texture;
                    r.cellW       = ts->cellPixelW;
                    r.cellH       = ts->cellPixelH;
                }
            }
        }
        return r;
    }

    // Render the painted cells of a TilemapComponent. Each cell sits in
    // a regular grid anchored on the entity's transform; the source UV
    // comes from the cell's tile index inside the texture (row-major,
    // zero = top-left of the texture).
    void DrawTilemapEntity(ImDrawList* dl, const SceneEditor::Entity& e,
                           const SceneEditor::TilemapComponent& tm,
                           const ImVec2& panelMin,
                           const ImVec2& panelSize, bool selected) {
        const ResolvedTileset rt = ResolveTileset(tm);
        if (rt.cellW <= 0 || rt.cellH <= 0) return;
        NaraGame::RHITexture* tex =
            m_assetDb.GetTexture(GetDevice(), rt.textureGuid);
        if (!tex || tex->Width() == 0 || tex->Height() == 0) return;
        const int texCols = int(tex->Width())  / rt.cellW;
        const int texRows = int(tex->Height()) / rt.cellH;
        if (texCols <= 0 || texRows <= 0) return;

        // World units per pixel: read the underlying texture's import
        // settings so the tilemap inherits the project's chosen PPU.
        SceneEditor::TextureImportSettings imp;
        if (const auto* entry = m_assetDb.FindByGuid(rt.textureGuid)) {
            imp.FromJson(entry->importSettings);
        }
        const float ppu        = imp.pixelsPerUnit;
        const float cellWorldW = float(rt.cellW) / ppu;
        const float cellWorldH = float(rt.cellH) / ppu;

        const float texW = float(tex->Width());
        const float texH = float(tex->Height());
        const ImVec2 entityWp = EntityWorldPos(e);
        const float  zoom     = m_viewCam.zoom;
        const ImVec2 c        = WorldToScreen(entityWp, panelMin, panelSize);
        const ImU32  tint     = IM_COL32(255, 255, 255, 255);
        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));
        // M7c17: Tilemap rotation. Every cell is drawn through
        // the same rigid-body rotation matrix the M7c12-c16
        // sprite paths use; the whole tilemap rotates around
        // the entity origin (gridX=0, gridY=0).
        const auto* trans =
            e.GetComponent<SceneEditor::TransformComponent>();
        const float rotDeg = trans ? trans->rotation : 0.0f;
        const float rotRad = rotDeg
            * 3.14159265358979323846f / 180.0f;
        const float cosR = std::cos(rotRad);
        const float sinR = std::sin(rotRad);
        auto Project = [&](float lx, float ly) {
            const float rx = cosR * lx - sinR * ly;
            const float ry = sinR * lx + cosR * ly;
            return ImVec2{c.x + rx * zoom, c.y - ry * zoom};
        };

        // Bounding box (in entity-local) used for the
        // selection outline + label position. We track it
        // in local coords + rotate the 4 corners at the end.
        float locMinX =  std::numeric_limits<float>::infinity();
        float locMinY =  std::numeric_limits<float>::infinity();
        float locMaxX = -std::numeric_limits<float>::infinity();
        float locMaxY = -std::numeric_limits<float>::infinity();

        for (const auto& kv : tm.cells) {
            const int gx  = kv.first.first;
            const int gy  = kv.first.second;
            const int idx = kv.second;
            if (idx < 0) continue;

            // Map tile index -> source pixel rect (row-major, Y-down
            // in source so row 0 sits at the texture's top).
            const int tileCol = idx % texCols;
            const int tileRow = idx / texCols;
            if (tileRow >= texRows) continue;
            const float u0 =  float(tileCol     * rt.cellW) / texW;
            const float v0 =  float(tileRow     * rt.cellH) / texH;
            const float u1 =  float((tileCol + 1) * rt.cellW) / texW;
            const float v1 =  float((tileCol + 1) * rt.cellH) / texH;

            // Cell rect in entity-local coords (gy=0 is the
            // bottom row of the tilemap; the texture's row 0
            // sits at the visual top of the cell, so the UV
            // top sits at the world top).
            const float locL =  gx       * cellWorldW;
            const float locR = (gx + 1) * cellWorldW;
            const float locB =  gy       * cellWorldH;
            const float locT = (gy + 1) * cellWorldH;
            locMinX = std::min(locMinX, locL);
            locMaxX = std::max(locMaxX, locR);
            locMinY = std::min(locMinY, locB);
            locMaxY = std::max(locMaxY, locT);

            const ImVec2 pTL = Project(locL, locT);
            const ImVec2 pTR = Project(locR, locT);
            const ImVec2 pBR = Project(locR, locB);
            const ImVec2 pBL = Project(locL, locB);
            // UV: top-left of source cell sits at the visual
            // top-left of the rotated quad; AddImageQuad maps
            // UVs to vertex positions in declared order.
            const float vu0 = float(tileCol     * rt.cellW) / texW;
            const float vu1 = float((tileCol + 1) * rt.cellW) / texW;
            const float vv0 = float(tileRow     * rt.cellH) / texH;
            const float vv1 = float((tileRow + 1) * rt.cellH) / texH;
            (void)u0; (void)v0; (void)u1; (void)v1;
            dl->AddImageQuad(texId, pTL, pTR, pBR, pBL,
                             ImVec2(vu0, vv0), ImVec2(vu1, vv0),
                             ImVec2(vu1, vv1), ImVec2(vu0, vv1),
                             tint);
        }

        // Collision overlay: walk the cells once more (now that we have
        // the same world-to-screen math handy) and paint a translucent
        // red box over every cell whose tile-index is flagged
        // collides in the bound Tileset asset. Skips entirely when the
        // toggle is off, the tilemap doesn't point at a Tileset asset,
        // or that tileset has no flagged tiles -- so the overhead is
        // strictly proportional to flagged cells.
        if (m_showCollisionOverlay) {
            if (const auto* tsEntry = m_assetDb.FindByGuid(tm.tileset)) {
                if (tsEntry->typeName == "Tileset") {
                    if (SceneEditor::TilesetAsset* ts =
                            m_assetDb.GetTileset(tm.tileset)) {
                        DrawTilemapCollisionOverlay(dl, tm, *ts, entityWp,
                                                    cellWorldW, cellWorldH,
                                                    c, zoom);
                    }
                }
            }
        }

        // Selection outline + label only when we drew at least one cell.
        // M7c17: outline traces the rotated tilemap outer rect.
        if (locMinX <= locMaxX && locMinY <= locMaxY) {
            const ImVec2 pTL = Project(locMinX, locMaxY);
            const ImVec2 pTR = Project(locMaxX, locMaxY);
            const ImVec2 pBR = Project(locMaxX, locMinY);
            const ImVec2 pBL = Project(locMinX, locMinY);
            if (selected) {
                const ImVec2 pts[5] = { pTL, pTR, pBR, pBL, pTL };
                dl->AddPolyline(pts, 5,
                                IM_COL32(255, 220, 60, 255),
                                ImDrawFlags_None, 2.0f);
            }
            float minX = pTL.x, maxY = pTL.y;
            for (const ImVec2& p : {pTR, pBR, pBL}) {
                minX = std::min(minX, p.x);
                maxY = std::max(maxY, p.y);
            }
            if (!m_standalone) {
                dl->AddText(ImVec2{minX, maxY + 2.0f},
                            IM_COL32(220, 220, 220, 255), e.name.c_str());
            }
        }
    }

    void DrawTilemapCollisionOverlay(
        ImDrawList* dl,
        const SceneEditor::TilemapComponent& tm,
        const SceneEditor::TilesetAsset& ts,
        const ImVec2& entityWp,
        float cellWorldW, float cellWorldH,
        const ImVec2& c, float zoom) {
        for (const auto& kv : tm.cells) {
            const int idx = kv.second;
            auto it = ts.tiles.find(idx);
            if (it == ts.tiles.end() || !it->second.collides) continue;
            const int gx = kv.first.first;
            const int gy = kv.first.second;
            const float worldL =  gx       * cellWorldW;
            const float worldR = (gx + 1) * cellWorldW;
            const float worldB =  gy       * cellWorldH;
            const float worldT = (gy + 1) * cellWorldH;
            const ImVec2 a{c.x + worldL * zoom, c.y - worldT * zoom};
            const ImVec2 b{c.x + worldR * zoom, c.y - worldB * zoom};
            dl->AddRectFilled(a, b, IM_COL32(220, 60, 60, 110));
            dl->AddRect(a, b, IM_COL32(255, 110, 110, 200), 0.0f, 0, 1.0f);
        }
        (void)entityWp;
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

    // Resolved sprite info: source rect (px) inside the texture, 9-slice
    // border (px), pivot. Sub-sprite reference takes precedence over
    // whole-texture defaults so multi-sprite sheets with mixed borders
    // / pivots work the way you'd expect.
    struct ResolvedSprite {
        int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
        int borderL = 0, borderR = 0, borderT = 0, borderB = 0;
        NaraGame::Vec2 pivot{0.5f, 0.5f};
    };

    ResolvedSprite Resolve(const SceneEditor::SpriteRendererComponent& spr,
                           const NaraGame::RHITexture* tex,
                           const SceneEditor::TextureImportSettings& imp) const {
        ResolvedSprite r;
        r.srcW    = int(tex->Width());
        r.srcH    = int(tex->Height());
        r.borderL = imp.borderLeft;
        r.borderR = imp.borderRight;
        r.borderT = imp.borderTop;
        r.borderB = imp.borderBottom;
        r.pivot   = imp.ResolvedPivot();
        if (!spr.subSprite.empty()) {
            for (const SceneEditor::SubSprite& s : imp.spriteSheet.sprites) {
                if (s.name == spr.subSprite) {
                    r.srcX = s.x; r.srcY = s.y;
                    r.srcW = s.w; r.srcH = s.h;
                    r.borderL = s.borderLeft;
                    r.borderR = s.borderRight;
                    r.borderT = s.borderTop;
                    r.borderB = s.borderBottom;
                    r.pivot   = s.pivot;
                    break;
                }
            }
        }
        return r;
    }

    static ImU32 ColorToImU32(const NaraGame::Color& c) {
        return IM_COL32(
            uint8_t(std::clamp(c.r, 0.0f, 1.0f) * 255.0f),
            uint8_t(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
            uint8_t(std::clamp(c.b, 0.0f, 1.0f) * 255.0f),
            uint8_t(std::clamp(c.a, 0.0f, 1.0f) * 255.0f));
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
        SceneEditor::TextureImportSettings imp;
        if (const auto* entry = m_assetDb.FindByGuid(spr.sprite)) {
            imp.FromJson(entry->importSettings);
        }
        const ResolvedSprite rs = Resolve(spr, tex, imp);

        // Sliced + Tiled both take a separate 9-zone draw path. Sliced
        // stretches the edges + centre; Tiled repeats them. Simple
        // keeps the original single AddImage call below.
        if (spr.drawMode == SceneEditor::SpriteDrawMode::Sliced) {
            DrawSlicedSpriteEntity(dl, e, spr, tex, imp, rs, c, selected);
            return;
        }
        if (spr.drawMode == SceneEditor::SpriteDrawMode::Tiled) {
            DrawTiledSpriteEntity(dl, e, spr, tex, imp, rs, c, selected);
            return;
        }

        float u0 = float(rs.srcX) / float(tex->Width());
        float v0 = float(rs.srcY) / float(tex->Height());
        float u1 = float(rs.srcX + rs.srcW) / float(tex->Width());
        float v1 = float(rs.srcY + rs.srcH) / float(tex->Height());
        const float wWorld = float(rs.srcW) / imp.pixelsPerUnit;
        const float hWorld = float(rs.srcH) / imp.pixelsPerUnit;
        const NaraGame::Vec2 pivot = rs.pivot;
        // World-space rect anchored on the entity's transform. The pivot
        // chooses which texel sits on the transform position: (0.5,0.5)
        // centres it, (0,0) puts the bottom-left of the sprite there,
        // and so on.
        const float worldLeft   = -pivot.x        * wWorld;
        const float worldRight  = (1.0f - pivot.x) * wWorld;
        const float worldBottom = -pivot.y        * hWorld;
        const float worldTop    = (1.0f - pivot.y) * hWorld;

        // UV flip swaps the resolved (u0, v0) / (u1, v1) endpoints in
        // place, so sub-sprite + flip combinations stay inside the
        // intended sheet cell.
        if (spr.flipX) std::swap(u0, u1);
        if (spr.flipY) std::swap(v0, v1);

        // M7c12: respect the Transform's rotation (degrees,
        // z-axis CCW). Each corner is rotated around the
        // entity's transform origin in world space before the
        // Y-flip + zoom mapping into screen pixels.
        const auto* trans = e.GetComponent<SceneEditor::TransformComponent>();
        const float rotDeg = trans ? trans->rotation : 0.0f;
        const float rotRad = rotDeg * 3.14159265358979323846f / 180.0f;
        const float cosR = std::cos(rotRad);
        const float sinR = std::sin(rotRad);
        const float z = m_viewCam.zoom;
        auto Project = [&](float wx, float wy) {
            const float rx = cosR * wx - sinR * wy;
            const float ry = sinR * wx + cosR * wy;
            return ImVec2{c.x + rx * z, c.y - ry * z};
        };
        // Four corners in CW order starting from top-left of
        // the sprite rect (world top = larger Y, which maps to
        // smaller screen Y after the Project Y-flip).
        const ImVec2 pTL = Project(worldLeft,  worldTop);
        const ImVec2 pTR = Project(worldRight, worldTop);
        const ImVec2 pBR = Project(worldRight, worldBottom);
        const ImVec2 pBL = Project(worldLeft,  worldBottom);

        const ImVec2 uvTL{u0, v0};
        const ImVec2 uvTR{u1, v0};
        const ImVec2 uvBR{u1, v1};
        const ImVec2 uvBL{u0, v1};
        const ImU32 tint = ColorToImU32(spr.color);
        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));
        dl->AddImageQuad(texId, pTL, pTR, pBR, pBL,
                         uvTL, uvTR, uvBR, uvBL, tint);

        // Selection outline traces the rotated quad.
        if (selected) {
            const ImVec2 pts[5] = { pTL, pTR, pBR, pBL, pTL };
            dl->AddPolyline(pts, 5,
                            IM_COL32(255, 220, 60, 255),
                            ImDrawFlags_None, 2.0f);
        }
        // Label sits below the (unrotated) AABB of the
        // four corners so it stays readable + clear of the
        // sprite regardless of rotation.
        float minY = pTL.y, maxY = pTL.y, minX = pTL.x;
        for (const ImVec2& p : {pTR, pBR, pBL}) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
            minX = std::min(minX, p.x);
        }
        if (!m_standalone) {
            dl->AddText(ImVec2{minX, maxY + 2.0f},
                        IM_COL32(220, 220, 220, 255),
                        e.name.c_str());
        }
    }

    // M7c16: rotation-aware AddImage helper. Projects a world
    // rect's 4 corners through the entity's rotation + the
    // viewport Y-flip + zoom, then submits an AddImageQuad. The
    // Sliced + Tiled paths build their 9-zone composition out
    // of these calls so the whole sprite rotates as a single
    // rigid quad even when split across 9 sub-rectangles.
    void AddImageRotated(ImDrawList* dl, ImTextureID texId,
                         const ImVec2& c,
                         float cosR, float sinR, float zoom,
                         float wL, float wR, float wB, float wT,
                         float u0, float v0, float u1, float v1,
                         ImU32 tint) const {
        auto P = [&](float wx, float wy) {
            const float rx = cosR * wx - sinR * wy;
            const float ry = sinR * wx + cosR * wy;
            return ImVec2{c.x + rx * zoom, c.y - ry * zoom};
        };
        const ImVec2 pTL = P(wL, wT);
        const ImVec2 pTR = P(wR, wT);
        const ImVec2 pBR = P(wR, wB);
        const ImVec2 pBL = P(wL, wB);
        const ImVec2 uvTL{u0, v0};
        const ImVec2 uvTR{u1, v0};
        const ImVec2 uvBR{u1, v1};
        const ImVec2 uvBL{u0, v1};
        dl->AddImageQuad(texId, pTL, pTR, pBR, pBL,
                         uvTL, uvTR, uvBR, uvBL, tint);
    }

    void DrawSlicedSpriteEntity(ImDrawList* dl,
                                const SceneEditor::Entity& e,
                                const SceneEditor::SpriteRendererComponent& spr,
                                const NaraGame::RHITexture* tex,
                                const SceneEditor::TextureImportSettings& imp,
                                const ResolvedSprite& rs,
                                const ImVec2& c, bool selected) {
        // Total destination size = SpriteRenderer.size (world units).
        // Clamp to the border sum so degenerate (size < border) doesn't
        // collapse the centre into a flipped quad.
        const float ppu = imp.pixelsPerUnit;
        const float bLw = float(rs.borderL) / ppu;
        const float bRw = float(rs.borderR) / ppu;
        const float bTw = float(rs.borderT) / ppu;
        const float bBw = float(rs.borderB) / ppu;
        const float totW = std::max(spr.size.x, bLw + bRw);
        const float totH = std::max(spr.size.y, bTw + bBw);

        // World rect anchored on the entity via pivot.
        const float worldLeft   = -rs.pivot.x        * totW;
        const float worldRight  = (1.0f - rs.pivot.x) * totW;
        const float worldBottom = -rs.pivot.y        * totH;
        const float worldTop    = (1.0f - rs.pivot.y) * totH;

        // 4 X positions (world) and 4 Y positions (world, Y-up).
        const float xs[4] = { worldLeft,
                              worldLeft  + bLw,
                              worldRight - bRw,
                              worldRight };
        const float ys[4] = { worldBottom,
                              worldBottom + bBw,
                              worldTop    - bTw,
                              worldTop };

        const float texW = float(tex->Width());
        const float texH = float(tex->Height());
        // 4 U / V positions in source pixel space, normalised.
        const float us[4] = {
            float(rs.srcX)                       / texW,
            float(rs.srcX + rs.borderL)          / texW,
            float(rs.srcX + rs.srcW - rs.borderR)/ texW,
            float(rs.srcX + rs.srcW)             / texW,
        };
        const float vs[4] = {
            float(rs.srcY)                       / texH,
            float(rs.srcY + rs.borderT)          / texH,
            float(rs.srcY + rs.srcH - rs.borderB)/ texH,
            float(rs.srcY + rs.srcH)             / texH,
        };

        const ImU32 tint = ColorToImU32(spr.color);
        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));

        // M7c16: rotation from Transform. The 9-zone composition
        // still walks in world coordinates; AddImageRotated
        // bends each tile through the rotation matrix before
        // submitting it as an AddImageQuad.
        const auto* trans =
            e.GetComponent<SceneEditor::TransformComponent>();
        const float rotDeg = trans ? trans->rotation : 0.0f;
        const float rotRad = rotDeg
            * 3.14159265358979323846f / 180.0f;
        const float cosR = std::cos(rotRad);
        const float sinR = std::sin(rotRad);
        const float zoom = m_viewCam.zoom;
        // Walk the 9 tiles. Column i (0=L, 1=C, 2=R) maps to source
        // column i. World row j (0=bottom, 1=mid, 2=top) maps to source
        // row (2-j) because the texture's Y=0 is the top edge.
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) {
                if (xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j]) continue;
                const int srcRow = 2 - j;
                AddImageRotated(dl, texId, c, cosR, sinR, zoom,
                                xs[i], xs[i + 1],
                                ys[j], ys[j + 1],
                                us[i], vs[srcRow],
                                us[i + 1], vs[srcRow + 1], tint);
            }
        }

        // Selection outline traces the rotated outer quad.
        auto P = [&](float wx, float wy) {
            const float rx = cosR * wx - sinR * wy;
            const float ry = sinR * wx + cosR * wy;
            return ImVec2{c.x + rx * zoom, c.y - ry * zoom};
        };
        const ImVec2 pTL = P(worldLeft,  worldTop);
        const ImVec2 pTR = P(worldRight, worldTop);
        const ImVec2 pBR = P(worldRight, worldBottom);
        const ImVec2 pBL = P(worldLeft,  worldBottom);
        if (selected) {
            const ImVec2 pts[5] = { pTL, pTR, pBR, pBL, pTL };
            dl->AddPolyline(pts, 5,
                            IM_COL32(255, 220, 60, 255),
                            ImDrawFlags_None, 2.0f);
        }
        float minX = pTL.x, maxY = pTL.y;
        for (const ImVec2& p : {pTR, pBR, pBL}) {
            minX = std::min(minX, p.x);
            maxY = std::max(maxY, p.y);
        }
        if (!m_standalone) {
            dl->AddText(ImVec2{minX, maxY + 2.0f},
                        IM_COL32(220, 220, 220, 255),
                        e.name.c_str());
        }
    }

    // Tile a source pixel rect across a world-space destination rect.
    // `tileX` / `tileY` decide whether each axis repeats (true) or
    // stretches the source span over the dest band (false). Partial
    // tiles at the trailing edge clip the UV span so the cut tile shows
    // the leading portion of the source (matching the seam pattern of
    // every other tile).
    void TileQuad(ImDrawList* dl, const ImVec2& c, ImTextureID texId,
                  int srcX, int srcY, int srcW, int srcH,
                  float worldL, float worldR, float worldT, float worldB,
                  float texW, float texH, float ppu, float zoom,
                  bool tileX, bool tileY, ImU32 tint,
                  float cosR = 1.0f, float sinR = 0.0f) const {
        if (worldR <= worldL || worldT <= worldB) return;
        if (srcW <= 0 || srcH <= 0) return;
        const float tileWorldW = float(srcW) / ppu;
        const float tileWorldH = float(srcH) / ppu;
        const float u0Base   = float(srcX)         / texW;
        const float v0Base   = float(srcY)         / texH;
        const float u1Full   = float(srcX + srcW)  / texW;
        const float v1Full   = float(srcY + srcH)  / texH;

        // Build the X / Y span lists. Each span is (worldA, worldB,
        // fracOfSource) where fracOfSource is the portion of the
        // source tile covered (1.0 for full, <1 for trailing partials).
        struct Span { float a; float b; float frac; };
        std::vector<Span> xs, ys;
        if (tileX) {
            for (float x = worldL; x < worldR; ) {
                const float next = std::min(x + tileWorldW, worldR);
                xs.push_back({x, next, (next - x) / tileWorldW});
                x = next;
            }
        } else {
            xs.push_back({worldL, worldR, 1.0f});
        }
        if (tileY) {
            // Y-up: walk top-down from worldT to worldB.
            for (float y = worldT; y > worldB; ) {
                const float next = std::max(y - tileWorldH, worldB);
                ys.push_back({next, y, (y - next) / tileWorldH});
                y = next;
            }
        } else {
            ys.push_back({worldB, worldT, 1.0f});
        }

        for (const Span& yspan : ys) {
            for (const Span& xspan : xs) {
                const float u1 = tileX
                    ? (float(srcX) + float(srcW) * xspan.frac) / texW
                    : u1Full;
                const float v1 = tileY
                    ? (float(srcY) + float(srcH) * yspan.frac) / texH
                    : v1Full;
                AddImageRotated(dl, texId, c, cosR, sinR, zoom,
                                xspan.a, xspan.b,
                                yspan.a, yspan.b,
                                u0Base, v0Base, u1, v1, tint);
            }
        }
    }

    void DrawTiledSpriteEntity(ImDrawList* dl,
                               const SceneEditor::Entity& e,
                               const SceneEditor::SpriteRendererComponent& spr,
                               const NaraGame::RHITexture* tex,
                               const SceneEditor::TextureImportSettings& imp,
                               const ResolvedSprite& rs,
                               const ImVec2& c, bool selected) {
        // 9-zone split identical to Sliced; the difference is that
        // edges + centre tile their source instead of stretching it.
        // border = 0 collapses every corner / edge to zero size so the
        // single centre zone tiles the whole texture across the dest.
        const float ppu = imp.pixelsPerUnit;
        const float bLw = float(rs.borderL) / ppu;
        const float bRw = float(rs.borderR) / ppu;
        const float bTw = float(rs.borderT) / ppu;
        const float bBw = float(rs.borderB) / ppu;
        const float totW = std::max(spr.size.x, bLw + bRw);
        const float totH = std::max(spr.size.y, bTw + bBw);
        const float worldLeft   = -rs.pivot.x        * totW;
        const float worldRight  = (1.0f - rs.pivot.x) * totW;
        const float worldBottom = -rs.pivot.y        * totH;
        const float worldTop    = (1.0f - rs.pivot.y) * totH;

        // 4 world X / Y boundaries (matches DrawSlicedSpriteEntity).
        const float xs[4] = { worldLeft, worldLeft + bLw,
                              worldRight - bRw, worldRight };
        const float ys[4] = { worldBottom, worldBottom + bBw,
                              worldTop - bTw, worldTop };

        // Inner pixel span of the source rect (corners not included).
        const int innerW = std::max(0, rs.srcW - rs.borderL - rs.borderR);
        const int innerH = std::max(0, rs.srcH - rs.borderT - rs.borderB);

        const float texW = float(tex->Width());
        const float texH = float(tex->Height());
        const float zoom = m_viewCam.zoom;
        const ImU32 tint = ColorToImU32(spr.color);
        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));
        // M7c16: rotation matrix for the Tiled mode mirrors the
        // Sliced + Simple paths. TileQuad threads cosR / sinR
        // through to each per-tile AddImageRotated call so the
        // whole tiled wall rotates as one rigid quad.
        const auto* trans =
            e.GetComponent<SceneEditor::TransformComponent>();
        const float rotDeg = trans ? trans->rotation : 0.0f;
        const float rotRad = rotDeg
            * 3.14159265358979323846f / 180.0f;
        const float cosR = std::cos(rotRad);
        const float sinR = std::sin(rotRad);

        // Helper to pick the source rect for (column i, row j) in the
        // 3x3 split. Row 0 (world bottom) -> source row 2 (texture top
        // is small Y, so source bottom = srcY + srcH - bB).
        auto SrcRect = [&](int i, int j, int& sx, int& sy,
                                   int& sw, int& sh) {
            const int srcRow = 2 - j;   // world row -> source row
            // X
            if (i == 0)      { sx = rs.srcX;                    sw = rs.borderL; }
            else if (i == 1) { sx = rs.srcX + rs.borderL;       sw = innerW;     }
            else             { sx = rs.srcX + rs.srcW - rs.borderR; sw = rs.borderR; }
            // Y (srcRow 0 = top of texture)
            if (srcRow == 0)      { sy = rs.srcY;                    sh = rs.borderT; }
            else if (srcRow == 1) { sy = rs.srcY + rs.borderT;       sh = innerH;     }
            else                  { sy = rs.srcY + rs.srcH - rs.borderB; sh = rs.borderB; }
        };

        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) {
                if (xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j]) continue;
                int sx, sy, sw, sh;
                SrcRect(i, j, sx, sy, sw, sh);
                if (sw <= 0 || sh <= 0) continue;
                const bool tileX = (i == 1);   // X tiles inside the middle column
                const bool tileY = (j == 1);   // Y tiles inside the middle row
                TileQuad(dl, c, texId, sx, sy, sw, sh,
                         xs[i], xs[i + 1], ys[j + 1], ys[j],
                         texW, texH, ppu, zoom, tileX, tileY, tint,
                         cosR, sinR);
            }
        }

        // Selection outline traces the rotated outer quad.
        auto P = [&](float wx, float wy) {
            const float rx = cosR * wx - sinR * wy;
            const float ry = sinR * wx + cosR * wy;
            return ImVec2{c.x + rx * zoom, c.y - ry * zoom};
        };
        const ImVec2 pTL = P(worldLeft,  worldTop);
        const ImVec2 pTR = P(worldRight, worldTop);
        const ImVec2 pBR = P(worldRight, worldBottom);
        const ImVec2 pBL = P(worldLeft,  worldBottom);
        if (selected) {
            const ImVec2 pts[5] = { pTL, pTR, pBR, pBL, pTL };
            dl->AddPolyline(pts, 5,
                            IM_COL32(255, 220, 60, 255),
                            ImDrawFlags_None, 2.0f);
        }
        float minX = pTL.x, maxY = pTL.y;
        for (const ImVec2& p : {pTR, pBR, pBL}) {
            minX = std::min(minX, p.x);
            maxY = std::max(maxY, p.y);
        }
        if (!m_standalone) {
            dl->AddText(ImVec2{minX, maxY + 2.0f},
                        IM_COL32(220, 220, 220, 255),
                        e.name.c_str());
        }
    }

    // ImGui callback for the REPL input. Up/Down cycle through
    // m_replHistory; -1 = "current draft" (no entry selected) and
    // arrowing past either end wraps to the draft. Restores the
    // text widget to whichever history entry the index lands on.
    static int ReplHistoryCallback(ImGuiInputTextCallbackData* data) {
        auto* app = static_cast<EditorApp*>(data->UserData);
        if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory ||
            !app || app->m_replHistory.empty()) return 0;
        const int last = int(app->m_replHistory.size()) - 1;
        if (data->EventKey == ImGuiKey_UpArrow) {
            if (app->m_replHistoryIdx == -1)
                app->m_replHistoryIdx = last;
            else if (app->m_replHistoryIdx > 0)
                --app->m_replHistoryIdx;
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (app->m_replHistoryIdx == -1) return 0;
            if (app->m_replHistoryIdx >= last)
                app->m_replHistoryIdx = -1;
            else
                ++app->m_replHistoryIdx;
        } else {
            return 0;
        }
        data->DeleteChars(0, data->BufTextLen);
        if (app->m_replHistoryIdx >= 0) {
            const std::string& s =
                app->m_replHistory[size_t(app->m_replHistoryIdx)];
            data->InsertChars(0, s.c_str());
        }
        return 0;
    }

    void DrawConsolePanel() {
        ImGui::Begin("Console");
        // Toolbar.
        if (ImGui::SmallButton("Clear##events")) m_eventLog.clear();
        ImGui::SameLine();
        ImGui::TextDisabled("Animation events: %zu", m_eventLog.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##script")) {
            m_scriptHost.ClearOutputLog();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Script output: %zu",
                            m_scriptHost.OutputLog().size());
        ImGui::Separator();
        // Split the panel: event log on top, script output below,
        // REPL input line at the very bottom (always reserves its
        // own row so the script-output child shrinks instead of
        // pushing the prompt off-screen).
        const float lineH      = ImGui::GetTextLineHeightWithSpacing();
        const float availTotal = ImGui::GetContentRegionAvail().y;
        const float promptH    = lineH + 6.0f;
        const float logsH      = availTotal - promptH;
        const float halfH      = std::max(40.0f, logsH * 0.5f);
        ImGui::BeginChild("##eventlog", ImVec2(0, halfH), false);
        for (auto it = m_eventLog.rbegin(); it != m_eventLog.rend(); ++it) {
            ImGui::TextUnformatted(it->c_str());
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::BeginChild("##scriptlog",
                          ImVec2(0, std::max(40.0f, logsH - halfH)),
                          false);
        const auto& sout = m_scriptHost.OutputLog();
        for (auto it = sout.rbegin(); it != sout.rend(); ++it) {
            ImGui::TextUnformatted(it->c_str());
        }
        ImGui::EndChild();
        ImGui::Separator();
        // REPL prompt.
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        bool reclaimFocus = false;
        const ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackHistory;
        if (ImGui::InputText("##repl", m_replInputBuffer,
                              sizeof(m_replInputBuffer), flags,
                              &EditorApp::ReplHistoryCallback,
                              this)) {
            const std::string code = m_replInputBuffer;
            if (!code.empty()) {
                m_scriptHost.AppendLine("> " + code);
                // Windows.h defines LoadString as a macro that
                // expands to LoadStringW/A -- undef it locally so
                // the ScriptHost member call goes through cleanly.
                #ifdef LoadString
                #undef LoadString
                #endif
                m_scriptHost.LoadString(code, "[repl]");
                m_replHistory.push_back(code);
                if (m_replHistory.size() > 64) {
                    m_replHistory.erase(m_replHistory.begin());
                }
            }
            m_replInputBuffer[0] = '\0';
            m_replHistoryIdx = -1;
            reclaimFocus = true;
        }
        if (reclaimFocus) ImGui::SetKeyboardFocusHere(-1);
        ImGui::End();
    }

    void DrawTilePalettePanel() {
        if (!ImGui::Begin("Tile Palette")) {
            ImGui::End();
            return;
        }
        // The palette is bound to whichever entity is currently
        // selected; without a Tilemap on that entity there's nothing to
        // paint into.
        SceneEditor::Entity* selEntity = m_scene.Get(m_selection);
        SceneEditor::TilemapComponent* tm = selEntity
            ? selEntity->GetComponent<SceneEditor::TilemapComponent>()
            : nullptr;
        if (!tm) {
            ImGui::TextDisabled("Select a Tilemap entity to paint.");
            ImGui::End();
            return;
        }
        if (!tm->tileset.IsValid()) {
            ImGui::TextDisabled("Tilemap has no tileset assigned.");
            ImGui::End();
            return;
        }
        const ResolvedTileset rt = ResolveTileset(*tm);
        NaraGame::RHITexture* tex =
            m_assetDb.GetTexture(GetDevice(), rt.textureGuid);
        if (!tex || tex->Width() == 0 || tex->Height() == 0) {
            ImGui::TextDisabled("Tileset texture unavailable.");
            ImGui::End();
            return;
        }
        if (rt.cellW <= 0 || rt.cellH <= 0) {
            ImGui::TextDisabled("Cell size invalid.");
            ImGui::End();
            return;
        }
        const int texCols = int(tex->Width())  / rt.cellW;
        const int texRows = int(tex->Height()) / rt.cellH;
        if (texCols <= 0 || texRows <= 0) {
            ImGui::TextDisabled("Cell size larger than texture.");
            ImGui::End();
            return;
        }

        // Toolbar: tool toggles + active tile indicator.
        auto toolBtn = [&](const char* label, PaintTool tool) {
            const bool active = (m_paintTool == tool);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                                              IM_COL32(220, 180, 70, 255));
            const bool clicked = ImGui::Button(label);
            if (active) ImGui::PopStyleColor();
            if (clicked) {
                m_paintTool = active ? PaintTool::None : tool;
            }
        };
        toolBtn("Brush",  PaintTool::Brush);
        ImGui::SameLine();
        toolBtn("Eraser", PaintTool::Eraser);
        ImGui::SameLine();
        toolBtn("Bucket", PaintTool::Bucket);
        ImGui::SameLine();
        toolBtn("Rect",   PaintTool::Rect);
        ImGui::SameLine();
        ImGui::TextDisabled("active tile: %d", m_paintTileIndex);
        // Collision-overlay toggle. Sits in the same toolbar so the
        // debug view + paint tools share one panel; it's a global
        // flag, not per-tilemap.
        ImGui::Checkbox("Show collision", &m_showCollisionOverlay);
        ImGui::Separator();

        // Autotile brush picker. Lists every .autotile asset in the
        // project; clicking one turns the next Brush / Rect placement
        // into an autotile paint that recomputes neighbour masks. The
        // "(none)" button reverts to the regular stamp brush.
        bool anyAutotile = false;
        for (const SceneEditor::AssetEntry& a : m_assetDb.Entries()) {
            if (a.typeName != "Autotile") continue;
            anyAutotile = true;
            const bool sel = (a.guid == m_activeAutotile);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                                            IM_COL32(220, 180, 70, 255));
            if (ImGui::SmallButton(a.relPath.c_str())) {
                m_activeAutotile = sel ? SceneEditor::AssetGuid{} : a.guid;
                if (m_paintTool == PaintTool::None) m_paintTool = PaintTool::Brush;
            }
            if (sel) ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        if (anyAutotile) {
            if (ImGui::SmallButton("none##autotile")) {
                m_activeAutotile = SceneEditor::AssetGuid{};
            }
            ImGui::Separator();
        }

        // Texture preview, scale-to-fit.
        const float texW  = float(tex->Width());
        const float texH  = float(tex->Height());
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = std::min(avail.x / texW, avail.y / texH);
        if (scale <= 0.0f) scale = 1.0f;
        const ImVec2 imgSize{texW * scale, texH * scale};
        const ImVec2 imgMin = ImGui::GetCursorScreenPos();

        const ImTextureID texId =
            static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(
                tex->GetNativeView()));
        ImGui::Image(texId, imgSize);

        const float cellPxScreenW = float(rt.cellW) * scale;
        const float cellPxScreenH = float(rt.cellH) * scale;

        // Capture mouse on top of the image. We own the left button so
        // we can drive a click-to-pick + drag-to-grow stamp selection.
        ImGui::SetCursorScreenPos(imgMin);
        ImGui::InvisibleButton("##palette_hit", imgSize,
                               ImGuiButtonFlags_MouseButtonLeft);
        const bool hovered  = ImGui::IsItemHovered();
        const bool clicked  = hovered &&
                              ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        const ImVec2 mp = ImGui::GetMousePos();
        const int mouseCol = std::clamp(
            int((mp.x - imgMin.x) / std::max(0.0001f, cellPxScreenW)),
            0, texCols - 1);
        const int mouseRow = std::clamp(
            int((mp.y - imgMin.y) / std::max(0.0001f, cellPxScreenH)),
            0, texRows - 1);

        if (clicked) {
            m_stampAnchorCol = mouseCol;
            m_stampAnchorRow = mouseRow;
            m_stampCurCol    = mouseCol;
            m_stampCurRow    = mouseRow;
            m_stampDragging  = true;
            if (m_paintTool == PaintTool::None) {
                m_paintTool = PaintTool::Brush;
            }
        } else if (m_stampDragging &&
                   ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_stampCurCol = mouseCol;
            m_stampCurRow = mouseRow;
        }
        if (released) m_stampDragging = false;
        // Keep m_paintTileIndex in sync with the stamp's top-left cell
        // so Bucket (and external readers) still have a single index.
        const int stampC0 = std::min(m_stampAnchorCol, m_stampCurCol);
        const int stampR0 = std::min(m_stampAnchorRow, m_stampCurRow);
        const int stampC1 = std::max(m_stampAnchorCol, m_stampCurCol);
        const int stampR1 = std::max(m_stampAnchorRow, m_stampCurRow);
        m_paintTileIndex = stampR0 * texCols + stampC0;

        // Grid overlay + stamp rect highlight.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 gridCol = IM_COL32(255, 255, 255, 60);
        for (int r = 0; r <= texRows; ++r) {
            const float y = imgMin.y + r * cellPxScreenH;
            dl->AddLine(ImVec2{imgMin.x,             y},
                        ImVec2{imgMin.x + imgSize.x, y}, gridCol, 1.0f);
        }
        for (int cc = 0; cc <= texCols; ++cc) {
            const float x = imgMin.x + cc * cellPxScreenW;
            dl->AddLine(ImVec2{x, imgMin.y},
                        ImVec2{x, imgMin.y + imgSize.y}, gridCol, 1.0f);
        }
        const ImVec2 selA{imgMin.x +  stampC0      * cellPxScreenW,
                          imgMin.y +  stampR0      * cellPxScreenH};
        const ImVec2 selB{imgMin.x + (stampC1 + 1) * cellPxScreenW,
                          imgMin.y + (stampR1 + 1) * cellPxScreenH};
        dl->AddRectFilled(selA, selB, IM_COL32(255, 220, 60, 35));
        dl->AddRect(selA, selB, IM_COL32(255, 220, 60, 255), 0.0f, 0, 2.0f);

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
        // Double-click a Texture row to open the Sprite Editor with
        // that asset as the active edit target.
        if (e && e->typeName == "Texture" &&
            ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            OpenSpriteEditor(e->guid);
        }
        if (e && e->typeName == "Tileset" &&
            ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            OpenTilesetEditor(e->guid);
        }
        if (e && e->typeName == "Autotile" &&
            ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            OpenAutotileEditor(e->guid);
        }
        if (e && e->typeName == "Animation" &&
            ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            OpenAnimationEditor(e->guid);
        }
        if (e && e->typeName == "AnimatorController" &&
            ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            OpenControllerEditor(e->guid);
        }
        if (e && e->typeName == "Script" &&
            ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            OpenScriptEditor(m_assetDb.AssetsRoot() + "/" + e->relPath,
                             e->relPath);
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

    // Undo / redo stacks. Each entry is a full Scene snapshot string;
    // simple and robust against the wide variety of mutation paths
    // (paint, gizmo drag, component add, reparent, ...). Capped so the
    // stack can't grow without bound during a long editing session.
    std::vector<std::string> m_undoStack;
    std::vector<std::string> m_redoStack;
    static constexpr size_t  kUndoStackMax = 64;

    SceneEditor::AssetDatabase m_assetDb;
    SceneEditor::AssetGuid     m_selectedAsset;
    // M8c6 per-asset audition gain. Keyed on AssetGuid so
    // the slider state survives Inspector redraws + lets
    // the user A/B test two clips at different volumes by
    // bouncing between them.
    std::unordered_map<SceneEditor::AssetGuid, float>
                               m_audioPreviewVolume;

    ViewportCamera             m_viewCam;

    // Translate-gizmo state. Hover updates every hovered frame; active
    // sticks from mouse-down to release. m_gizmoDownWorld + m_gizmoStartPos
    // are the anchors the per-frame delta is applied against.
    GizmoAxis      m_gizmoHover     = GizmoAxis::None;
    GizmoAxis      m_gizmoActive    = GizmoAxis::None;
    ImVec2         m_gizmoDownWorld{0.0f, 0.0f};
    NaraGame::Vec3 m_gizmoStartPos{0.0f, 0.0f, 0.0f};

    // Modal trigger flags.
    bool                m_openLayersModal = false;
    bool                m_openCollisionMatrixModal = false;
    bool                m_openAudioMixerModal = false;
    bool                m_openEditorPrefsModal = false;

    // UI scale (fonts + padding). Seeded from DetectUiScale on start
    // and overridden by editor.prefs.json if the file exists. The
    // slider in the Editor Preferences modal writes into this field
    // and flips m_pendingFontRebuild; the next OnRender wipes the
    // font atlas + re-uploads it before ImGui::NewFrame so the change
    // shows up on the very next frame without a restart.
    float               m_uiScale            = 1.0f;
    bool                m_pendingFontRebuild = false;
    // Editor color theme. Persisted alongside uiScale. Radio in the
    // Editor Preferences modal writes into this and flips
    // m_pendingThemeRefresh so the next OnRender re-applies the
    // palette. A dedicated flag (rather than reusing the font one)
    // lets the theme change without paying the atlas rebuild cost.
    EditorTheme         m_theme                = EditorTheme::Dark;
    bool                m_pendingThemeRefresh  = false;
    // M10c1 settings overlay rebind capture. Empty when no rebind
    // is in progress; set to the action name when the user clicks
    // "Rebind" + cleared on the next key press (which writes the
    // captured key into ScriptHost's action binding map).
    std::string         m_settingsRebindAction;

    // M10c3 scene transition fade. ConsumePendingSceneLoad drives
    // a tiny FadingOut -> swap -> FadingIn state machine when a
    // Lua scene.load(path, {fade_out=, fade_in=, color=}) call
    // sets non-zero fade times. Idle = nothing drawn / nothing
    // pending; the transient color + timer fields are only
    // meaningful while state != Idle.
    enum class TransitionState { Idle, FadingOut, FadingIn };
    TransitionState m_transitionState   = TransitionState::Idle;
    float           m_transitionElapsed = 0.0f;
    float           m_transitionFadeOut = 0.0f;
    float           m_transitionFadeIn  = 0.0f;
    float           m_transitionColorR  = 0.0f;
    float           m_transitionColorG  = 0.0f;
    float           m_transitionColorB  = 0.0f;
    std::string     m_transitionPendingPath;
    // Sprite Editor state. m_spriteEditorOpen drives the window's
    // visibility; activating it via OpenSpriteEditor pre-focuses the
    // window so a stale floating instance doesn't get lost behind a
    // dock when the user double-clicks an asset.
    bool                m_spriteEditorOpen     = false;
    bool                m_spriteEditorActivate = false;
    SceneEditor::AssetGuid m_spriteEditorAsset;
    // Tileset Editor follows the same activation pattern.
    bool                m_tilesetEditorOpen     = false;
    bool                m_tilesetEditorActivate = false;
    SceneEditor::AssetGuid m_tilesetEditorAsset;
    // Autotile Editor mirrors the Sprite / Tileset pattern. The active
    // slot is the 0..15 mask index the user has selected for editing;
    // clicking a tile in the preview writes that index into the
    // autotile's mask[activeSlot].
    bool                m_autotileEditorOpen     = false;
    bool                m_autotileEditorActivate = false;
    SceneEditor::AssetGuid m_autotileEditorAsset;
    int                 m_autotileEditorSlot = 0;
    // Animation Editor: editor-local scrubber + autoplay drive the
    // preview without touching the AnimatorComponent's playback state,
    // so opening the .nanim in the editor never mutates the live scene.
    bool                m_animEditorOpen     = false;
    bool                m_animEditorActivate = false;
    SceneEditor::AssetGuid m_animEditorAsset;
    float               m_animEditorScrubTime = 0.0f;
    bool                m_animEditorAutoplay  = true;
    // Index of the event the user picked via the row "->" button.
    // The scrubber marker and the matching row jump to its time when
    // clicked; -1 = no event selected.
    int                 m_animEditorSelEvent  = -1;
    // Substring filter for the Events list. Empty buffer shows
    // every row; otherwise the row loop hides events whose names
    // don't contain the buffer (case-insensitive). The scrubber +
    // preview-dot markers stay unfiltered so the user still sees
    // every event's firing position at a glance.
    char                m_animEditorEventFilter[64] = "";
    // When on, the editor sorts the events vector by time after
    // any add or time edit so the rows + markers stay in playback
    // order without the user clicking the M5c25 "Sort by time"
    // button.
    bool                m_animEditorAutoSort  = false;
    // Sprite-name substring filter for the Frames list. Empty
    // buffer shows every row; reordering frames stays a manual
    // step because the row order IS the animation playback order.
    char                m_animEditorFrameFilter[64] = "";
    // "Apply to all" duration value used by the bulk-edit button.
    float               m_animEditorApplyDuration   = 0.1f;
    // Editor-side clipboards used by the Frames + Events Copy /
    // Paste triggers. Outlive any single Animation Editor session
    // so the user can copy from one .nanim, swap assets, and paste
    // into another. Not serialised.
    std::vector<SceneEditor::AnimationFrame>      m_clipboardFrames;
    std::vector<SceneEditor::AnimationEvent>      m_clipboardEvents;
    // Same cross-asset clipboard pattern for the Controller
    // Editor's three vectors. Transitions reference state indices,
    // so the paste path clamps out-of-range fromIdx + toIdx to
    // safe defaults (-1 / 0) when the target controller has fewer
    // states than the source.
    std::vector<SceneEditor::AnimatorState>      m_clipboardStates;
    std::vector<SceneEditor::AnimatorParameter>  m_clipboardParameters;
    std::vector<SceneEditor::AnimatorTransition> m_clipboardTransitions;
    // Parameter filters used by the controller editor + the per-
    // entity Animator inspector. Both treat an empty buffer as
    // "show every parameter" and otherwise match the parameter name
    // against the buffer case-insensitively as a substring. The
    // companion type-filter ints take 0 = "All" and 1..7 = the
    // matching AnimatorParameter::Type ordinal + 1.
    char                m_controllerEditorParamFilter[64] = "";
    char                m_animInspectorParamFilter[64]    = "";
    int                 m_controllerEditorParamTypeFilter = 0;
    int                 m_animInspectorParamTypeFilter    = 0;
    // Transition list filters in the Controller Editor. 0 = show
    // every row. For "From" 1 = the (global) fromIdx == -1 bucket,
    // 2..N+1 = state[i - 2]. For "To" 1..N = state[i - 1]. For
    // "Param" 1 = the empty-param "(none)" bucket, 2..M+1 =
    // parameter[i - 2].
    int                 m_controllerEditorTrFromFilter    = 0;
    int                 m_controllerEditorTrToFilter      = 0;
    int                 m_controllerEditorTrParamFilter   = 0;
    // Animator Controller Editor follows the same pattern.
    bool                m_controllerEditorOpen     = false;
    bool                m_controllerEditorActivate = false;
    SceneEditor::AssetGuid m_controllerEditorAsset;
    // Graph view selection + drag state. -1 = no selection / no drag.
    int                 m_graphSelectedState      = -1;
    int                 m_graphDragState          = -1;
    int                 m_graphSelectedTransition = -1;
    // Interactive transition authoring. m_graphPendingFrom holds the
    // source state index while we wait for the user to click a
    // target; m_graphContextNode caches whichever node fired the
    // right-click context menu.
    int                 m_graphPendingFrom   = -1;
    int                 m_graphContextNode   = -1;
    int                 m_spriteEditorMode  = 0;    // 0=by size, 1=by count
    int                 m_spriteEditorCellW = 32;
    int                 m_spriteEditorCellH = 32;
    int                 m_spriteEditorCols  = 4;
    int                 m_spriteEditorRows  = 4;
    int                 m_spriteEditorSel   = -1;
    // Manual-mode drag state. Pixel coords are integers so the live
    // rubber-band matches what gets persisted on release.
    ManualDrag          m_manualDrag        = ManualDrag::None;
    int                 m_manualAnchorPx[2] = {0, 0};
    int                 m_manualCurPx[2]    = {0, 0};
    // Border-line drag / hover. Lives separately from m_manualDrag so
    // border editing works in every slice mode, not just Manual.
    BorderDragSide      m_borderDrag        = BorderDragSide::None;
    BorderDragSide      m_borderHover       = BorderDragSide::None;
    // Debug overlay for the tilemap's collides flag. When ON, every
    // painted cell whose tile-index is flagged in the bound Tileset
    // asset gets a translucent red highlight on top of the cell.
    bool                m_showCollisionOverlay = false;

    // Editor-side animation playback. When true, OnRender ticks every
    // AnimatorComponent in the scene and writes the sampled frame's
    // sub-sprite name into the companion SpriteRenderer so the user
    // sees the animation live in the viewport. Off freezes the
    // currently sampled frame.
    bool                m_animPlaying          = true;
    // Rolling log of recently fired AnimationEvents. Console panel
    // shows the tail; capped so a runaway emitter can't grow without
    // bound.
    std::vector<std::string> m_eventLog;
    static constexpr size_t  kEventLogMax = 64;
    // Editor-side scripting subsystem. M6c1 ships the dispatch
    // wiring + output log; the actual Lua bridge will land in a
    // follow-up commit once the host has more event sources to
    // route. The host outlives every Animation Editor session.
    SceneEditor::ScriptHost  m_scriptHost;

    // Polling file watcher for hot-reloadable asset types. Every
    // kAssetWatchInterval seconds the editor walks every entry in
    // the database + dispatches the right reload path when its
    // last_write_time bumped:
    //   Script             -> ScriptHost::LoadFile (M6c3)
    //   Animation          -> AssetDatabase::EvictAnimation
    //   AnimatorController -> AssetDatabase::EvictAnimatorController
    //   Texture            -> AssetDatabase::EvictTexture
    //   Tileset            -> AssetDatabase::EvictTileset
    //   Autotile           -> AssetDatabase::EvictAutotile
    // Non-trackable types (Scene, Prefab, Model, Audio, Font,
    // SpriteSheet, HdrEnvironment, Unknown) drop through without
    // mtime bookkeeping so the cache stays bounded.
    float                    m_assetWatchAccum   = 0.0f;
    static constexpr float   kAssetWatchInterval = 1.0f;
    // M11c4 structural-change watcher. The on-disk file count from
    // the last poll. A mismatch on the next poll triggers a Rescan
    // so freshly-dropped files become visible in the Project panel
    // + inspector list without a manual refresh.
    size_t                   m_assetWatchLastCount = 0;
    std::unordered_map<std::string, std::filesystem::file_time_type>
                             m_assetMTimes;

    // Minimal in-editor text editor for .lua scripts. Opens via
    // double-click on a Script-typed Project row; the buffer is
    // a fixed 64 KB block so the InputTextMultiline widget has a
    // single contiguous backing store. Files that exceed 64 KB
    // load truncated + a tag in the title flags it so the user
    // doesn't accidentally save back a chopped file.
    // Active overlap pairs from the previous TickColliders call.
    // Keyed by the symmetric pair `(min(id), max(id))`. Each
    // tick we rebuild the new pair set and diff against this to
    // fire OnTriggerEnter for newly-overlapping pairs and
    // OnTriggerExit for ones that left contact.
    std::set<std::pair<SceneEditor::EntityId,
                       SceneEditor::EntityId>>
                        m_collisionPairs;
    // M7c18: per-tile collider synthetic IDs map to their parent
    // tilemap entity's display name. The collision system treats
    // each flagged tile as an independent static box (so a body
    // hitting a corner sees the slab face it actually contacted),
    // but events report the tilemap's name as "other" so script
    // handlers don't need to know about per-cell synthetic ids.
    std::unordered_map<SceneEditor::EntityId, std::string>
                        m_syntheticColliderNames;

    // REPL input line at the bottom of the Console panel. Up /
    // Down arrows scroll through submitted history; Enter dispatches
    // the buffer through ScriptHost::LoadString + echoes it into
    // the host's output log as `> <code>` so the response sits
    // right after the prompt.
    char                m_replInputBuffer[1024] = "";
    std::vector<std::string> m_replHistory;
    int                 m_replHistoryIdx = -1;

    bool                m_scriptEditorOpen     = false;
    bool                m_scriptEditorActivate = false;
    std::string         m_scriptEditorAbsPath;
    std::string         m_scriptEditorRelPath;
    bool                m_scriptEditorTruncated = false;
    std::vector<char>   m_scriptEditorBuffer;
    static constexpr size_t kScriptEditorBufSize = 64 * 1024;

    // Tilemap paint state. The active stamp is a rectangular region of
    // palette cells defined by (Anchor, Cur) in palette (column, row)
    // coordinates. A single click leaves Anchor == Cur so the stamp is
    // 1x1, matching the M4c2/M4c3 behaviour. Drag in the palette grows
    // it. m_paintTileIndex stays in sync with the top-left palette cell
    // so Bucket (which doesn't tile) still has a single tile to use.
    enum class PaintTool { None, Brush, Eraser, Bucket, Rect };
    PaintTool           m_paintTool          = PaintTool::None;
    int                 m_paintTileIndex     = 0;
    int                 m_stampAnchorCol     = 0;
    int                 m_stampAnchorRow     = 0;
    int                 m_stampCurCol        = 0;
    int                 m_stampCurRow        = 0;
    bool                m_stampDragging      = false;
    // Active autotile brush. When valid, Brush + Rect paint flows
    // recompute neighbour masks instead of placing the literal stamp.
    SceneEditor::AssetGuid m_activeAutotile;
    // Rectangle drag in progress. The anchor is captured on mouse-down;
    // m_rectCur tracks the cursor each frame so the ghost outline
    // grows / shrinks with the drag.
    bool                m_rectActive        = false;
    int                 m_rectAnchorGx      = 0;
    int                 m_rectAnchorGy      = 0;
    int                 m_rectCurGx         = 0;
    int                 m_rectCurGy         = 0;
    // New Project modal state.
    bool                m_openNewProjectModal = false;
    std::array<char, 256> m_newProjectName{};
    std::array<char, 1024> m_newProjectPath{};
    std::string         m_newProjectError;
    // Deferred native folder-picker request. Firing IFileOpenDialog
    // ::Show inside a live BeginPopupModal freezes the app -- the
    // dialog's own message loop keeps our WM_PAINT servicing the
    // half-built ImGui frame, and the picker ends up behind the
    // popup. Instead, the Browse button just sets this flag; the
    // very end of OnRender (after ImGui has rendered + submitted
    // its DrawData) fires the picker with the ImGui frame fully
    // committed to the backbuffer. UX-wise indistinguishable, but
    // the dialog now lands on top and stays interactive.
    bool                m_pendingBrowseForNewProjectPath = false;

    std::string m_startupProjectPath;
    std::string m_startupScenePath;
    // M9c1 standalone runtime mode. When true the editor boots
    // straight into a viewport-only window with Play forced on
    // -- no menu bar, no panels, no dockspace. Used to ship a
    // playable build from the same exe; long-term this will
    // split into its own `game` target with packed assets.
    bool        m_standalone = false;
};

} // namespace

Application* NaraGame::CreateApplication(int argc, char** argv) {
    std::string project, scene, buildDest;
    bool standalone = false;
    // Two-pass parse: the flag arg lives on its own (no value),
    // the project/scene args eat the next argv. Keep the same
    // "i + 1 < argc" guard from before for the pair-style flags +
    // a separate scan for the bare --standalone toggle so a user
    // can put it anywhere on the command line.
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--standalone") standalone = true;
    }
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--project") project = argv[++i];
        else if (a == "--scene") scene = argv[++i];
        else if (a == "--build-game") buildDest = argv[++i];
    }
    // M9c13 headless build mode. `--project <p> --build-game
    // <dest>` opens the project, packs Assets/ into
    // <dest>/assets.pak, copies <dest>/ProjectSettings/ +
    // <dest>/game.exe, and exits without opening a window.
    // Lets CI / snap scripts produce a packed game without
    // walking the editor's menu.
    if (!buildDest.empty() && !project.empty()) {
        SceneEditor::RegisterBuiltinComponents();
        SceneEditor::Project p;
        if (!p.Open(project)) {
            std::fprintf(stderr,
                "build-game: cannot open project '%s'\n",
                project.c_str());
            std::exit(1);
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::exists(buildDest, ec) &&
            fs::is_directory(buildDest, ec) &&
            fs::directory_iterator(buildDest, ec) !=
                fs::directory_iterator()) {
            std::fprintf(stderr,
                "build-game: '%s' is not empty\n",
                buildDest.c_str());
            std::exit(1);
        }
        fs::create_directories(buildDest, ec);
        wchar_t exePathW[MAX_PATH] = { 0 };
        if (!GetModuleFileNameW(nullptr, exePathW, MAX_PATH)) {
            std::fprintf(stderr,
                "build-game: GetModuleFileName failed\n");
            std::exit(1);
        }
        const fs::path destExe =
            fs::path(buildDest) / L"game.exe";
        fs::copy_file(fs::path(exePathW), destExe,
            fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr,
                "build-game: cannot copy exe (%s)\n",
                ec.message().c_str());
            std::exit(1);
        }
        const fs::path pakPath =
            fs::path(buildDest) / L"assets.pak";
        if (!SceneEditor::WritePak(pakPath.string(),
                                     p.AssetsDir())) {
            std::fprintf(stderr,
                "build-game: WritePak failed\n");
            std::exit(1);
        }
        fs::copy(p.SettingsDir(),
                  (fs::path(buildDest) / L"ProjectSettings").string(),
                  fs::copy_options::recursive |
                  fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr,
                "build-game: ProjectSettings copy failed (%s)\n",
                ec.message().c_str());
            std::exit(1);
        }
        std::fprintf(stdout,
            "build-game: '%s' -> %s ok\n",
            p.Name().c_str(), buildDest.c_str());
        std::exit(0);
    }
    // M9c12 / M9c13 auto-standalone. When the user double-
    // clicks a built game.exe with no args, look for the
    // project tree sitting alongside it (./ProjectSettings/
    // project.json) and fall straight into the standalone
    // runtime path. The editor's own scene-editor.exe usually
    // launches with no args too, so we only enable this when
    // the project file is actually present in the cwd.
    if (project.empty() && !standalone) {
        namespace fs = std::filesystem;
        std::error_code ec;
        // M9c13 first: if a packed `assets.pak` sits next to
        // the exe + Assets/ hasn't already been extracted,
        // unpack it now. The pak preserves the same
        // Assets/-relative tree the rest of the asset DB +
        // loaders already speak, so once it's on disk the
        // standalone path takes over unchanged.
        const bool havePak = fs::exists("assets.pak", ec);
        const bool haveAssetsDir =
            fs::is_directory("Assets", ec) &&
            fs::directory_iterator("Assets", ec) !=
                fs::directory_iterator();
        if (havePak) {
            // M9c17 mount in-memory regardless; vfs lookups
            // (Scene::Load, Lua script load) hit RAM first +
            // never need a disk Assets/ for those particular
            // call sites.
            SceneEditor::MountPak("assets.pak");
        }
        if (havePak && !haveAssetsDir) {
            // Extract path stays the fallback for loaders that
            // haven't been hooked yet (textures, audio): they
            // still read off disk so the extracted Assets/ has
            // to exist.
            if (!SceneEditor::ExtractPak("assets.pak", "Assets")) {
                NARAGAME_ERROR(
                    "auto-standalone: assets.pak failed to "
                    "extract; falling back to editor mode.");
            }
        }
        if (fs::exists("ProjectSettings/project.json", ec)) {
            project    = ".";
            standalone = true;
            if (scene.empty()) {
                // Read mainScene out of project.json so the
                // exe knows which scene to boot into without
                // needing a --scene argument. A tiny inline
                // read is cheaper than spinning up Project::Open
                // twice (here + inside OnStart).
                SceneEditor::Project probe;
                if (probe.Open(".")) {
                    const std::string& main = probe.MainScenePath();
                    if (!main.empty()) {
                        scene = "Assets/" + main;
                    }
                }
            }
        }
    }
    return new EditorApp(std::move(project), std::move(scene), standalone);
}
