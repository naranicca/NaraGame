#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_ANDROID)

#include "NaraGame/platform/Window.h"

namespace NaraGame {

// TODO: Implement with NativeActivity / ANativeWindow (android_native_app_glue).
// This stub satisfies the interface only.
class AndroidWindow final : public Window {
public:
    explicit AndroidWindow(const WindowDesc& desc) : m_desc(desc) {}

    void PollEvents() override {}
    bool ShouldClose() const override { return m_shouldClose; }
    void RequestClose() override { m_shouldClose = true; }

    uint32_t Width() const override { return m_desc.width; }
    uint32_t Height() const override { return m_desc.height; }
    void* NativeHandle() const override { return m_nativeWindow; }
    const WindowDesc& Desc() const override { return m_desc; }

private:
    WindowDesc m_desc;
    void* m_nativeWindow = nullptr; // ANativeWindow*
    bool m_shouldClose = false;
};

} // namespace NaraGame

#endif
