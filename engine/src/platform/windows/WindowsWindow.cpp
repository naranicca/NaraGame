#include "platform/windows/WindowsWindow.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)

#include "NaraGame/core/Log.h"
#include "NaraGame/input/Input.h"

#include <string>

namespace NaraGame {

static const wchar_t* kWindowClassName = L"NaraWindowClass";

static std::wstring Widen(const std::string& utf8) {
    if (utf8.empty())
        return std::wstring();
    const int len =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

static Key TranslateKey(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z')
        return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(vk - 'A'));
    if (vk >= '0' && vk <= '9')
        return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(vk - '0'));

    switch (vk) {
        case VK_SPACE:   return Key::Space;
        case VK_RETURN:  return Key::Enter;
        case VK_ESCAPE:  return Key::Escape;
        case VK_TAB:     return Key::Tab;
        case VK_BACK:    return Key::Backspace;
        case VK_LEFT:    return Key::Left;
        case VK_RIGHT:   return Key::Right;
        case VK_UP:      return Key::Up;
        case VK_DOWN:    return Key::Down;
        case VK_HOME:    return Key::Home;
        case VK_END:     return Key::End;
        case VK_PRIOR:   return Key::PageUp;
        case VK_NEXT:    return Key::PageDown;
        case VK_SHIFT:   return Key::LeftShift;
        case VK_CONTROL: return Key::LeftCtrl;
        case VK_MENU:    return Key::LeftAlt;
        case VK_OEM_4:   return Key::LeftBracket;   // [{
        case VK_OEM_6:   return Key::RightBracket;  // ]}
        case VK_F1:      return Key::F1;
        case VK_F2:      return Key::F2;
        case VK_F3:      return Key::F3;
        case VK_F4:      return Key::F4;
        case VK_F5:      return Key::F5;
        case VK_F6:      return Key::F6;
        case VK_F7:      return Key::F7;
        case VK_F8:      return Key::F8;
        case VK_F9:      return Key::F9;
        case VK_F10:     return Key::F10;
        case VK_F11:     return Key::F11;
        case VK_F12:     return Key::F12;
        default:         return Key::Unknown;
    }
}

WindowsWindow::WindowsWindow(const WindowDesc& desc)
    : m_desc(desc), m_width(desc.width), m_height(desc.height) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = &WindowsWindow::WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWindowClassName;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!desc.resizable)
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

    RECT rect{0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height)};
    AdjustWindowRect(&rect, style, FALSE);

    const std::wstring title = Widen(desc.title);
    m_hwnd = CreateWindowExW(0, kWindowClassName, title.c_str(), style,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left, rect.bottom - rect.top,
                             nullptr, nullptr, instance, this);
    if (m_hwnd == nullptr) {
        NARAGAME_ERROR("CreateWindowExW failed (0x%08lX)", GetLastError());
        return;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
}

WindowsWindow::~WindowsWindow() {
    if (m_hwnd != nullptr)
        DestroyWindow(m_hwnd);
}

void WindowsWindow::SetSize(uint32_t width, uint32_t height) {
    if (!m_hwnd || m_fullscreen || width == 0 || height == 0) return;
    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
    AdjustWindowRect(&rect, style, FALSE);
    SetWindowPos(m_hwnd, nullptr, 0, 0, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
}

void WindowsWindow::SetFullscreen(bool fullscreen) {
    if (!m_hwnd || fullscreen == m_fullscreen) return;
    if (fullscreen) {
        m_windowedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_hwnd, &m_windowedPlacement);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left,
                     monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                     SWP_FRAMECHANGED);
    } else {
        DWORD style = WS_OVERLAPPEDWINDOW;
        if (!m_desc.resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);
        SetWindowPlacement(m_hwnd, &m_windowedPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    m_fullscreen = fullscreen;
}

void WindowsWindow::PollEvents() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK WindowsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    auto* self = reinterpret_cast<WindowsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr)
        return self->HandleMessage(hwnd, msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT WindowsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    m_hwnd = hwnd;

    // Editor / tooling hook: ImGui's Win32 backend needs to see every
    // WM_* event before the engine's input router consumes it (or
    // they'd never reach widgets). The hook returns true to swallow
    // the message; otherwise we fall through to engine handling. Cast
    // widths explicitly so the hook signature doesn't pin us to the
    // Win32 typedefs in the public header.
    if (m_messageHook) {
        if (m_messageHook(hwnd, static_cast<unsigned int>(msg),
                          static_cast<unsigned long long>(wParam),
                          static_cast<long long>(lParam))) {
            return 0;
        }
    }

    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            m_shouldClose = true;
            return 0;

        case WM_SIZE:
            m_width = LOWORD(lParam);
            m_height = HIWORD(lParam);
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            Input::SetKeyState(TranslateKey(wParam), true);
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            Input::SetKeyState(TranslateKey(wParam), false);
            return 0;

        case WM_CHAR: {
            // ASCII printable + the editor-significant control codes:
            // backspace (0x08), enter (0x0D), escape (0x1B). Skip the
            // DEL (0x7F) and other controls.
            const wchar_t wc = static_cast<wchar_t>(wParam);
            if (wc == 0x08 || wc == 0x0D || wc == 0x1B ||
                (wc >= 0x20 && wc < 0x7F)) {
                Input::PushTextInput(static_cast<char>(wc));
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            // LOWORD/HIWORD give unsigned shorts; cast to signed short so points
            // outside the client area (e.g. during capture) report negative coords.
            const int mx = static_cast<short>(LOWORD(lParam));
            const int my = static_cast<short>(HIWORD(lParam));
            Input::SetMousePosition(static_cast<float>(mx), static_cast<float>(my));
            return 0;
        }

        // SetCapture on press / ReleaseCapture on release so the engine keeps
        // tracking the cursor (drag terraforming, marquee select) when it leaves
        // the window. Capture is per-button-down event; releasing any button
        // releases the capture — fine for a polling-style API.
        case WM_LBUTTONDOWN: Input::SetMouseButton(MouseButton::Left, true);   SetCapture(hwnd); return 0;
        case WM_LBUTTONUP:   Input::SetMouseButton(MouseButton::Left, false);  ReleaseCapture(); return 0;
        case WM_RBUTTONDOWN: Input::SetMouseButton(MouseButton::Right, true);  SetCapture(hwnd); return 0;
        case WM_RBUTTONUP:   Input::SetMouseButton(MouseButton::Right, false); ReleaseCapture(); return 0;
        case WM_MBUTTONDOWN: Input::SetMouseButton(MouseButton::Middle, true); SetCapture(hwnd); return 0;
        case WM_MBUTTONUP:   Input::SetMouseButton(MouseButton::Middle, false);ReleaseCapture(); return 0;

        case WM_MOUSEWHEEL: {
            // HIWORD(wParam) is a signed short delta in multiples of WHEEL_DELTA
            // (120). Convert to notches so a single wheel detent reads as ±1.0.
            const float notches = static_cast<float>(static_cast<short>(HIWORD(wParam))) / 120.0f;
            Input::AddMouseWheel(notches);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
