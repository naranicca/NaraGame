#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)

#include "NaraGame/platform/Window.h"

#include <windows.h>

namespace NaraGame {

class WindowsWindow final : public Window {
public:
    explicit WindowsWindow(const WindowDesc& desc);
    ~WindowsWindow() override;

    void PollEvents() override;
    bool ShouldClose() const override { return m_shouldClose; }
    void RequestClose() override { m_shouldClose = true; }

    uint32_t Width() const override { return m_width; }
    uint32_t Height() const override { return m_height; }
    void* NativeHandle() const override { return m_hwnd; }
    const WindowDesc& Desc() const override { return m_desc; }
    void SetSize(uint32_t width, uint32_t height) override;
    void SetFullscreen(bool fullscreen) override;
    bool IsFullscreen() const override { return m_fullscreen; }

    void SetMessageHook(WindowMessageHook hook) override { m_messageHook = hook; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    WindowDesc m_desc;
    HWND m_hwnd = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_shouldClose = false;
    bool m_fullscreen = false;
    WINDOWPLACEMENT m_windowedPlacement{sizeof(WINDOWPLACEMENT)};
    WindowMessageHook m_messageHook = nullptr;
};

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
