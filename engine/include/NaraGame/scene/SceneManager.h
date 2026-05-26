#pragma once

#include "NaraGame/scene/Scene.h"

#include <memory>
#include <vector>

namespace NaraGame {

// A stack of scenes. Change() replaces the whole stack; Push()/Pop() overlay
// (for pause menus, dialogs). Transitions requested from within a scene's
// OnUpdate are deferred and applied at safe points, so a scene can replace
// itself mid-update without being destroyed underneath its own call.
class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager();

    void Change(std::unique_ptr<Scene> scene); // clear stack, then push `scene`
    void Push(std::unique_ptr<Scene> scene);   // overlay on top
    void Pop();                                 // remove the top scene
    void Clear();

    void Update(float dt);
    void Render();

    Scene* Top() const { return m_stack.empty() ? nullptr : m_stack.back().get(); }
    bool Empty() const { return m_stack.empty(); }

private:
    enum class Action { Push, Pop, Change, Clear };
    struct Pending {
        Action action;
        std::unique_ptr<Scene> scene;
    };

    void ApplyPending();
    void DoPush(std::unique_ptr<Scene> scene);
    void DoPop();
    void DoClear();

    std::vector<std::unique_ptr<Scene>> m_stack;
    std::vector<Pending> m_pending;
};

} // namespace NaraGame
