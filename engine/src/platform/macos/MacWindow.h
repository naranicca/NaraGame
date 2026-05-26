#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_MACOS)

#include "NaraGame/platform/Window.h"

namespace NaraGame {

// TODO: Implement with Cocoa (NSWindow + NSView). The real version must be
// compiled as Objective-C++ (.mm). This stub only satisfies the interface.
class MacWindow final : public Window {
public:
    explicit MacWindow(const WindowDesc& desc) : m_desc(desc) {}

    void PollEvents() override {}
    bool ShouldClose() const override { return m_shouldClose; }
    void RequestClose() override { m_shouldClose = true; }

    uint32_t Width() const override { return m_desc.width; }
    uint32_t Height() const override { return m_desc.height; }
    void* NativeHandle() const override { return nullptr; }
    const WindowDesc& Desc() const override { return m_desc; }

private:
    WindowDesc m_desc;
    bool m_shouldClose = false;
};

} // namespace NaraGame

#endif
