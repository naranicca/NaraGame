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
        case VK_SHIFT:   return Key::LeftShift;
        case VK_CONTROL: return Key::LeftCtrl;
        case VK_MENU:    return Key::LeftAlt;
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

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
