#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>

namespace {
class MacPlayer final : public NaraGame::Application {
public:
    MacPlayer() : Application({"NaraGame", 1280, 720, true}) {}
protected:
    void OnRender() override {
        GetDevice().Clear({0.08f, 0.09f, 0.12f, 1.0f});
    }
};
}

NaraGame::Application* NaraGame::CreateApplication(int, char**) {
    return new MacPlayer();
}
