#include "platform/ios/iOSWindow.h"

#if defined(NARAGAME_PLATFORM_IOS)

namespace NaraGame {
namespace {
void* g_metalLayer = nullptr;
uint32_t g_width = 0;
uint32_t g_height = 0;
}

void SetIOSMetalLayer(void* layer, uint32_t width, uint32_t height) {
    g_metalLayer = layer;
    g_width = width;
    g_height = height;
}

iOSWindow::iOSWindow(const WindowDesc& desc)
    : m_desc(desc), m_metalLayer(g_metalLayer),
      m_width(g_width), m_height(g_height) {
    if (m_width == 0) m_width = desc.width;
    if (m_height == 0) m_height = desc.height;
    if (m_metalLayer == nullptr) m_shouldClose = true;
}

} // namespace NaraGame

#endif
