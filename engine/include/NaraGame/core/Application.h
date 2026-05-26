#pragma once

#include "NaraGame/audio/AudioDevice.h"
#include "NaraGame/platform/Window.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"

#include <cstdint>
#include <memory>
#include <string>

namespace NaraGame {

struct AppConfig {
    std::string title = "NaraGame";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool vsync = true;
};

// Base class for a game. Owns the window + RHI device and drives the main loop.
// Override the On* hooks; the engine calls them at the right time.
class Application {
public:
    explicit Application(AppConfig config = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();
    void Close();

    Window& GetWindow() { return *m_window; }
    RHIDevice& GetDevice() { return *m_device; }
    AudioDevice& GetAudio() { return *m_audio; }
    const AppConfig& Config() const { return m_config; }

    static Application& Get();

protected:
    virtual void OnStart() {}
    virtual void OnUpdate(float /*dt*/) {}
    virtual void OnRender() {}
    virtual void OnStop() {}

private:
    AppConfig m_config;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<RHIDevice> m_device;
    std::unique_ptr<AudioDevice> m_audio;
    bool m_running = true;
    uint32_t m_lastWidth = 0;
    uint32_t m_lastHeight = 0;

    static Application* s_instance;
};

// Implemented by the game (client). Returns a newly allocated Application.
Application* CreateApplication(int argc, char** argv);

} // namespace NaraGame
