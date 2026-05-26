#include "nara/core/Application.h"

#include "nara/core/Assert.h"
#include "nara/core/Log.h"
#include "nara/core/Time.h"
#include "nara/input/Input.h"

#include <utility>

namespace nara {

Application* Application::s_instance = nullptr;

Application::Application(AppConfig config) : m_config(std::move(config)) {
    NARA_ASSERT(s_instance == nullptr, "Application already exists");
    s_instance = this;

    WindowDesc windowDesc;
    windowDesc.title = m_config.title;
    windowDesc.width = m_config.width;
    windowDesc.height = m_config.height;
    m_window = Window::Create(windowDesc);
    NARA_ASSERT(m_window != nullptr, "Failed to create window");

    RHIDeviceDesc deviceDesc;
    deviceDesc.nativeWindowHandle = m_window->NativeHandle();
    deviceDesc.width = m_window->Width();
    deviceDesc.height = m_window->Height();
    deviceDesc.vsync = m_config.vsync;
    m_device = RHIDevice::Create(deviceDesc);
    NARA_ASSERT(m_device != nullptr, "Failed to create RHI device");

    m_audio = AudioDevice::Create();

    m_lastWidth = m_window->Width();
    m_lastHeight = m_window->Height();

    NARA_INFO("Initialized: window %ux%u, render %s, audio %s",
              m_window->Width(), m_window->Height(),
              m_device ? m_device->BackendName() : "none",
              m_audio ? m_audio->BackendName() : "none");
}

Application::~Application() {
    s_instance = nullptr;
}

Application& Application::Get() {
    return *s_instance;
}

void Application::Close() {
    m_running = false;
}

void Application::Run() {
    OnStart();

    Clock clock;
    while (m_running && !m_window->ShouldClose()) {
        const float dt = clock.Tick();

        Input::NewFrame();
        m_window->PollEvents();

        const uint32_t width = m_window->Width();
        const uint32_t height = m_window->Height();
        if ((width != m_lastWidth || height != m_lastHeight) && width > 0 && height > 0) {
            m_device->Resize(width, height);
            m_lastWidth = width;
            m_lastHeight = height;
        }

        OnUpdate(dt);
        m_audio->Update(); // reclaim finished sound voices

        // Skip rendering while minimized (zero-sized client area).
        if (width > 0 && height > 0) {
            m_device->BeginFrame();
            OnRender();
            m_device->EndFrame();
            m_device->Present();
        }
    }

    OnStop();
}

} // namespace nara
