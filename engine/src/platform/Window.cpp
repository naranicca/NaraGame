#include "NaraGame/platform/Window.h"

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)
    #include "platform/windows/WindowsWindow.h"
#elif defined(NARAGAME_PLATFORM_MACOS)
    #include "platform/macos/MacWindow.h"
#elif defined(NARAGAME_PLATFORM_IOS)
    #include "platform/ios/iOSWindow.h"
#elif defined(NARAGAME_PLATFORM_ANDROID)
    #include "platform/android/AndroidWindow.h"
#endif

namespace NaraGame {

std::unique_ptr<Window> Window::Create(const WindowDesc& desc) {
#if defined(NARAGAME_PLATFORM_WINDOWS)
    return std::make_unique<WindowsWindow>(desc);
#elif defined(NARAGAME_PLATFORM_MACOS)
    return std::make_unique<MacWindow>(desc);
#elif defined(NARAGAME_PLATFORM_IOS)
    return std::make_unique<iOSWindow>(desc);
#elif defined(NARAGAME_PLATFORM_ANDROID)
    return std::make_unique<AndroidWindow>(desc);
#else
    (void)desc;
    return nullptr;
#endif
}

} // namespace NaraGame
