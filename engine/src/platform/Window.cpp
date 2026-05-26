#include "nara/platform/Window.h"

#include "nara/core/Platform.h"

#if defined(NARA_PLATFORM_WINDOWS)
    #include "platform/windows/WindowsWindow.h"
#elif defined(NARA_PLATFORM_MACOS)
    #include "platform/macos/MacWindow.h"
#elif defined(NARA_PLATFORM_IOS)
    #include "platform/ios/iOSWindow.h"
#elif defined(NARA_PLATFORM_ANDROID)
    #include "platform/android/AndroidWindow.h"
#endif

namespace nara {

std::unique_ptr<Window> Window::Create(const WindowDesc& desc) {
#if defined(NARA_PLATFORM_WINDOWS)
    return std::make_unique<WindowsWindow>(desc);
#elif defined(NARA_PLATFORM_MACOS)
    return std::make_unique<MacWindow>(desc);
#elif defined(NARA_PLATFORM_IOS)
    return std::make_unique<iOSWindow>(desc);
#elif defined(NARA_PLATFORM_ANDROID)
    return std::make_unique<AndroidWindow>(desc);
#else
    (void)desc;
    return nullptr;
#endif
}

} // namespace nara
