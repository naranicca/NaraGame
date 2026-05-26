#include "NaraGame/scene/SceneManager.h"

#include <utility>

namespace NaraGame {

SceneManager::~SceneManager() {
    DoClear();
}

void SceneManager::Change(std::unique_ptr<Scene> scene) {
    m_pending.push_back({Action::Change, std::move(scene)});
}

void SceneManager::Push(std::unique_ptr<Scene> scene) {
    m_pending.push_back({Action::Push, std::move(scene)});
}

void SceneManager::Pop() {
    m_pending.push_back({Action::Pop, nullptr});
}

void SceneManager::Clear() {
    m_pending.push_back({Action::Clear, nullptr});
}

void SceneManager::DoPush(std::unique_ptr<Scene> scene) {
    if (!scene)
        return;
    scene->m_manager = this;
    m_stack.push_back(std::move(scene));
    m_stack.back()->OnEnter();
}

void SceneManager::DoPop() {
    if (m_stack.empty())
        return;
    m_stack.back()->OnExit();
    m_stack.pop_back();
}

void SceneManager::DoClear() {
    while (!m_stack.empty())
        DoPop();
}

void SceneManager::ApplyPending() {
    if (m_pending.empty())
        return;
    // Take a local copy so scenes scheduling more transitions in OnEnter/OnExit
    // are handled on the next pass rather than corrupting this iteration.
    std::vector<Pending> pending = std::move(m_pending);
    m_pending.clear();

    for (Pending& p : pending) {
        switch (p.action) {
            case Action::Change: DoClear(); DoPush(std::move(p.scene)); break;
            case Action::Push: DoPush(std::move(p.scene)); break;
            case Action::Pop: DoPop(); break;
            case Action::Clear: DoClear(); break;
        }
    }
}

void SceneManager::Update(float dt) {
    ApplyPending();
    if (m_stack.empty())
        return;

    // Update from the top down, stopping after the first scene that blocks.
    const int top = static_cast<int>(m_stack.size()) - 1;
    int start = 0;
    for (int i = top; i >= 0; --i) {
        start = i;
        if (m_stack[i]->BlocksUpdate())
            break;
    }
    for (int i = start; i <= top; ++i)
        m_stack[i]->OnUpdate(dt);

    ApplyPending(); // apply any transitions requested during this update
}

void SceneManager::Render() {
    if (m_stack.empty())
        return;

    // Render from the lowest scene that still needs drawing, upward.
    const int top = static_cast<int>(m_stack.size()) - 1;
    int start = 0;
    for (int i = top; i >= 0; --i) {
        start = i;
        if (m_stack[i]->BlocksRender())
            break;
    }
    for (int i = start; i <= top; ++i)
        m_stack[i]->OnRender();
}

} // namespace NaraGame
