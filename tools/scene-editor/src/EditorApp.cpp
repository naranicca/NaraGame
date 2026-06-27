// scene-editor -- Unity-style scene editor shell. This commit only sets
// up the Dear ImGui (docking) integration on top of the NaraGame engine:
// the host Application loop drives the frame, ImGui's Win32 backend
// receives raw messages through Window::SetMessageHook, and ImGui's
// D3D11 backend draws into the engine's backbuffer. Five placeholder
// panels (Hierarchy / Inspector / Viewport / Console / Project) sit in
// the dockspace so future commits can fill them with real content.

#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/platform/Window.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

#include <d3d11.h>

// Forward-declared by imgui_impl_win32.h with C++ linkage. We wrap it
// through the engine's untyped MessageHook signature below.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace NaraGame;

namespace {

constexpr uint32_t kWindowW = 1600;
constexpr uint32_t kWindowH = 900;

// Hook routed through Window::SetMessageHook. Always feeds ImGui first
// so its IO stays in sync; returns false so the engine's WM_SIZE /
// WM_CLOSE bookkeeping still runs. We don't try to gate engine input
// against ImGui's WantCapture* here -- the editor doesn't read engine
// Input::, so a duplicate state update is harmless.
bool ImGuiMessageHook(void* nativeHandle, unsigned int msg,
                      unsigned long long wparam,
                      long long lparam) {
    HWND hwnd  = reinterpret_cast<HWND>(nativeHandle);
    WPARAM wp  = static_cast<WPARAM>(wparam);
    LPARAM lp  = static_cast<LPARAM>(lparam);
    ImGui_ImplWin32_WndProcHandler(hwnd, static_cast<UINT>(msg), wp, lp);
    return false;
}

class EditorApp : public Application {
public:
    EditorApp() : Application(MakeConfig()) {}

    void OnStart() override {
        // Engine handles are already alive: GetWindow() owns the HWND,
        // GetDevice() owns the D3D11 device + context. Pull the native
        // pointers and hand them to the matching ImGui backends.
        HWND hwnd = static_cast<HWND>(GetWindow().NativeHandle());
        auto* dev = static_cast<ID3D11Device*>(GetDevice().GetNativeDevice());
        auto* ctx = static_cast<ID3D11DeviceContext*>(GetDevice().GetNativeContext());
        if (!hwnd || !dev || !ctx) {
            NARAGAME_ERROR("scene-editor: missing native window or D3D11 device handles");
            Close();
            return;
        }

        IMGUI_CHECKVERSION();
        m_imguiContext = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // Editor preferences (window layout, last-opened panels) get
        // serialized next to the executable. Future commit: route to
        // the user's project folder.
        io.IniFilename = "scene-editor.ini";
        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(dev, ctx);

        // Install the bridge so WM_* events reach ImGui. The engine
        // window owns the lifetime; we'll clear the hook in OnStop.
        GetWindow().SetMessageHook(&ImGuiMessageHook);

        NARAGAME_INFO("scene-editor: ImGui %s + DX11 + dockspace ready",
                      ImGui::GetVersion());
    }

    void OnStop() override {
        GetWindow().SetMessageHook(nullptr);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        if (m_imguiContext) {
            ImGui::DestroyContext(m_imguiContext);
            m_imguiContext = nullptr;
        }
    }

    void OnRender() override {
        GetDevice().Clear(Color(0.10f, 0.11f, 0.13f, 1.0f));

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Dockspace covering the whole main viewport. PassthruCentralNode
        // would let viewport content show through; we draw the backbuffer
        // colour ourselves so the default opaque dockspace is fine.
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Top menu bar -- minimal for now, future commits add File/Edit/Window.
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit", "Alt+F4")) Close();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                ImGui::MenuItem("ImGui Demo", nullptr, &m_showDemo);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Placeholder panels. Each is a normal ImGui window that the user
        // can dock to any side of the main viewport. Future commits will
        // move these into dedicated panel classes under panels/.
        DrawPlaceholder("Hierarchy",
            "Scene graph will land here -- entities, parenting, drag-drop.");
        DrawPlaceholder("Inspector",
            "Selected entity's components + property editors will land here.");
        DrawPlaceholder("Viewport",
            "2D ortho viewport + grid will render into an offscreen RT and "
            "display here. (3D toggle is a follow-up milestone.)");
        DrawPlaceholder("Console",
            "Editor + script log output will land here.");
        DrawPlaceholder("Project",
            "Asset browser + thumbnails will land here.");

        if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

private:
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title  = "NaraGame Scene Editor";
        c.width  = kWindowW;
        c.height = kWindowH;
        return c;
    }

    static void DrawPlaceholder(const char* title, const char* hint) {
        ImGui::Begin(title);
        ImGui::TextWrapped("%s", hint);
        ImGui::Separator();
        ImGui::TextDisabled("(placeholder, M1 commit 1)");
        ImGui::End();
    }

    ImGuiContext* m_imguiContext = nullptr;
    bool          m_showDemo     = false;
};

} // namespace

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new EditorApp();
}
