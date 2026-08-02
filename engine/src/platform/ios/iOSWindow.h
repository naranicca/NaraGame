#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_IOS)

#include "NaraGame/platform/Window.h"

namespace NaraGame {

class iOSWindow final : public Window {
public:
    explicit iOSWindow(const WindowDesc& desc);

    void PollEvents() override;
    bool ShouldClose() const override { return m_shouldClose; }
    void RequestClose() override { m_shouldClose = true; }

    uint32_t Width() const override { return m_width; }
    uint32_t Height() const override { return m_height; }
    void* NativeHandle() const override { return m_metalLayer; }
    const WindowDesc& Desc() const override { return m_desc; }

private:
    WindowDesc m_desc;
    void* m_metalLayer = nullptr; // CAMetalLayer*
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_shouldClose = false;
};

// UIKit host calls this before constructing Application/iOSWindow.
void SetIOSMetalLayer(void* layer, uint32_t width, uint32_t height);

} // namespace NaraGame

#endif
