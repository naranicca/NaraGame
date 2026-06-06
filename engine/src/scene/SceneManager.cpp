#include "NaraGame/scene/SceneManager.h"

#include <algorithm>
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

void SceneManager::ChangeWith(std::unique_ptr<Scene> scene, float durationSec) {
    if (m_transition.active || !scene) return;
    m_transition = Transition{};
    m_transition.active    = true;
    m_transition.duration  = durationSec > 0.0f ? durationSec : 0.0001f;
    m_transition.next      = std::move(scene);
    m_transition.kind      = TransitionKind::Fade;
    m_transition.color     = Color::Black();
}

void SceneManager::ChangeWithFadeWhite(std::unique_ptr<Scene> scene, float durationSec) {
    if (m_transition.active || !scene) return;
    m_transition = Transition{};
    m_transition.active    = true;
    m_transition.duration  = durationSec > 0.0f ? durationSec : 0.0001f;
    m_transition.next      = std::move(scene);
    m_transition.kind      = TransitionKind::Fade;
    m_transition.color     = Color::White();
}

void SceneManager::ChangeWithFadeColor(std::unique_ptr<Scene> scene, Color color,
                                       float durationSec) {
    if (m_transition.active || !scene) return;
    m_transition = Transition{};
    m_transition.active    = true;
    m_transition.duration  = durationSec > 0.0f ? durationSec : 0.0001f;
    m_transition.next      = std::move(scene);
    m_transition.kind      = TransitionKind::Fade;
    m_transition.color     = color;
}

void SceneManager::ChangeWithWipe(std::unique_ptr<Scene> scene, WipeDirection dir,
                                  float durationSec, Color color) {
    if (m_transition.active || !scene) return;
    m_transition = Transition{};
    m_transition.active    = true;
    m_transition.duration  = durationSec > 0.0f ? durationSec : 0.0001f;
    m_transition.next      = std::move(scene);
    m_transition.kind      = TransitionKind::Wipe;
    m_transition.color     = color;
    m_transition.direction = dir;
}

void SceneManager::ChangeWithCrossfade(std::unique_ptr<Scene> scene, float durationSec) {
    if (m_transition.active || !scene) return;
    m_transition = Transition{};
    m_transition.active    = true;
    m_transition.duration  = durationSec > 0.0f ? durationSec : 0.0001f;
    m_transition.next      = std::move(scene);
    m_transition.kind      = TransitionKind::Crossfade;
}

void SceneManager::ChangeWithPixelate(std::unique_ptr<Scene> scene, float durationSec) {
    if (m_transition.active || !scene) return;
    m_transition = Transition{};
    m_transition.active    = true;
    m_transition.duration  = durationSec > 0.0f ? durationSec : 0.0001f;
    m_transition.next      = std::move(scene);
    m_transition.kind      = TransitionKind::Pixelate;
}

bool SceneManager::IsBeforeSwap() const {
    return m_transition.active && !m_transition.swapped;
}

float SceneManager::TransitionProgress() const {
    if (!m_transition.active) return 0.0f;
    return std::min(1.0f, m_transition.elapsed / m_transition.duration);
}

float SceneManager::FadeAlpha() const {
    if (!m_transition.active || m_transition.kind != TransitionKind::Fade)
        return 0.0f;
    const float half = m_transition.duration * 0.5f;
    if (m_transition.elapsed <= half)
        return m_transition.elapsed / half;
    const float rem = m_transition.duration - m_transition.elapsed;
    return rem > 0.0f ? rem / half : 0.0f;
}

Vec4 SceneManager::WipeOverlayRectNorm() const {
    if (!m_transition.active || m_transition.kind != TransitionKind::Wipe)
        return {0.0f, 0.0f, 0.0f, 0.0f};
    const float p     = m_transition.elapsed / m_transition.duration;
    const bool wipeIn = p < 0.5f;
    const float f     = wipeIn ? (p / 0.5f) : ((p - 0.5f) / 0.5f);
    switch (m_transition.direction) {
        case WipeDirection::LeftToRight:
            return wipeIn ? Vec4{0.0f, 0.0f, f, 1.0f}
                          : Vec4{f, 0.0f, 1.0f - f, 1.0f};
        case WipeDirection::RightToLeft:
            return wipeIn ? Vec4{1.0f - f, 0.0f, f, 1.0f}
                          : Vec4{0.0f, 0.0f, 1.0f - f, 1.0f};
        case WipeDirection::TopToBottom:
            return wipeIn ? Vec4{0.0f, 0.0f, 1.0f, f}
                          : Vec4{0.0f, f, 1.0f, 1.0f - f};
        case WipeDirection::BottomToTop:
            return wipeIn ? Vec4{0.0f, 1.0f - f, 1.0f, f}
                          : Vec4{0.0f, 0.0f, 1.0f, 1.0f - f};
    }
    return {0.0f, 0.0f, 0.0f, 0.0f};
}

void SceneManager::Update(float dt) {
    if (m_transition.active) {
        m_transition.elapsed += dt;
        const float half = m_transition.duration * 0.5f;
        if (!m_transition.swapped && m_transition.elapsed >= half) {
            DoClear();
            DoPush(std::move(m_transition.next));
            m_transition.swapped = true;
        }
        if (m_transition.elapsed >= m_transition.duration) {
            m_transition.active = false;
            m_transition.next.reset();
        }
    }
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
