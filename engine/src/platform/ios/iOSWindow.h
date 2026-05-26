#pragma once

#include "nara/core/Platform.h"

#if defined(NARA_PLATFORM_IOS)

#include "nara/platform/Window.h"

namespace nara {

// TODO: Implement with UIKit (UIWindow + CAMetalLayer-backed UIView). The real
// version must be compiled as Objective-C++ (.mm). This stub satisfies the
// interface only.
class iOSWindow final : public Window {
public:
    explicit iOSWindow(const WindowDesc& desc) : m_desc(desc) {}

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

} // namespace nara

#endif
