#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_MACOS)

#include "NaraGame/platform/Window.h"

namespace NaraGame {

class MacWindow final : public Window {
public:
    explicit MacWindow(const WindowDesc& desc);
    ~MacWindow() override;

    void PollEvents() override;
    bool ShouldClose() const override { return m_shouldClose; }
    void RequestClose() override { m_shouldClose = true; }

    uint32_t Width() const override { return m_width; }
    uint32_t Height() const override { return m_height; }
    void* NativeHandle() const override { return m_metalLayer; }
    const WindowDesc& Desc() const override { return m_desc; }

private:
    WindowDesc m_desc;
    void* m_window = nullptr;     // NSWindow*
    void* m_view = nullptr;       // NSView*
    void* m_delegate = nullptr;   // id<NSWindowDelegate>
    void* m_metalLayer = nullptr; // CAMetalLayer*
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_shouldClose = false;
};

} // namespace NaraGame

#endif
