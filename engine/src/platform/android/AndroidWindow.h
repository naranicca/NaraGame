#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_ANDROID)

#include "NaraGame/platform/Window.h"
#include <android_native_app_glue.h>

namespace NaraGame {

class AndroidWindow final : public Window {
public:
    explicit AndroidWindow(const WindowDesc& desc);
    ~AndroidWindow() override;

    void PollEvents() override;
    bool ShouldClose() const override { return m_shouldClose; }
    void RequestClose() override { m_shouldClose = true; }

    uint32_t Width() const override { return m_width; }
    uint32_t Height() const override { return m_height; }
    void* NativeHandle() const override { return m_nativeWindow; }
    const WindowDesc& Desc() const override { return m_desc; }

private:
    static void HandleCommand(android_app* app, int32_t command);
    static int32_t HandleInput(android_app* app, AInputEvent* event);
    void RefreshNativeWindow();

    WindowDesc m_desc;
    android_app* m_app = nullptr;
    void* m_nativeWindow = nullptr; // ANativeWindow*
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_shouldClose = false;
};

// Called by EntryPoint's android_main before Application construction.
void SetAndroidApp(android_app* app);

} // namespace NaraGame

#endif
