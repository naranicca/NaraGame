#pragma once

namespace nara {

class SceneManager;

// A game state (title, gameplay, pause, ...). Scenes live in a SceneManager
// stack. By default a scene is opaque: scenes below it are neither updated nor
// rendered. Override Blocks* to let lower scenes show through (e.g. a pause
// overlay that freezes but still draws the gameplay beneath it).
class Scene {
public:
    virtual ~Scene() = default;

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void OnUpdate(float /*dt*/) {}
    virtual void OnRender() {}

    virtual bool BlocksUpdate() const { return true; }
    virtual bool BlocksRender() const { return true; }

    SceneManager& Manager() const { return *m_manager; }

private:
    SceneManager* m_manager = nullptr;
    friend class SceneManager;
};

} // namespace nara
