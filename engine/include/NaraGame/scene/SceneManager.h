#pragma once

#include "NaraGame/math/Vec4.h"
#include "NaraGame/renderer/rhi/RHITypes.h"
#include "NaraGame/scene/Scene.h"

#include <memory>
#include <vector>

namespace NaraGame {

// What shape of overlay the current transition draws.
//   Fade      — full-screen colour quad whose alpha follows an in/out curve.
//   Wipe      — colour quad that sweeps across the screen, fully covering it
//               at the halfway swap, then sweeping off the opposite side.
//   Crossfade — host app captures the old scene to a render target before
//               the swap, then alpha-blends that texture over the new scene
//               for the second half. Manager only manages timing/state; the
//               actual RT capture & blend lives in the host's OnRender.
//   Pixelate  — like Crossfade, but the host app samples the captured RT
//               with a quantised UV grid so the old frame looks chunky as it
//               fades out (Phase 2.B; falls back to Crossfade if the host
//               app hasn't implemented the mosaic pass yet).
enum class TransitionKind { None, Fade, Wipe, Crossfade, Pixelate };

enum class WipeDirection { LeftToRight, RightToLeft, TopToBottom, BottomToTop };

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

    // Schedule a scene change wrapped in a visual transition. The current
    // stack stays up during the first half (`fade-out` / `wipe-in`), the
    // swap happens exactly at the halfway mark, and the second half plays
    // the inverse animation (`fade-in` / `wipe-out`). Calls while a
    // transition is already running are ignored, so a key press during a
    // fade can't queue a second change. The manager does NOT draw the
    // overlay itself — the host app queries IsTransitioning() and the
    // helpers below from its render loop, then issues the quad(s).
    void ChangeWith(std::unique_ptr<Scene> scene, float durationSec = 0.3f);   // FadeBlack
    void ChangeWithFadeWhite(std::unique_ptr<Scene> scene, float durationSec = 0.3f);
    void ChangeWithFadeColor(std::unique_ptr<Scene> scene, Color color,
                             float durationSec = 0.3f);
    void ChangeWithWipe(std::unique_ptr<Scene> scene, WipeDirection dir,
                        float durationSec = 0.3f, Color color = Color::Black());
    // Phase-2 transitions that require the host to maintain an off-screen
    // RT of the outgoing scene. Manager doesn't own the RT; it only flips
    // the kind so OnRender can branch. Use IsBeforeSwap() to know whether
    // you should be capturing the outgoing frame or sampling it back.
    void ChangeWithCrossfade(std::unique_ptr<Scene> scene, float durationSec = 0.3f);
    void ChangeWithPixelate (std::unique_ptr<Scene> scene, float durationSec = 0.4f);

    void Update(float dt);
    void Render();

    Scene* Top() const { return m_stack.empty() ? nullptr : m_stack.back().get(); }
    bool Empty() const { return m_stack.empty(); }

    bool IsTransitioning() const { return m_transition.active; }
    TransitionKind TransitionType() const { return m_transition.kind; }
    Color          TransitionColor() const { return m_transition.color; }
    WipeDirection  TransitionWipeDirection() const { return m_transition.direction; }
    // True while we're still on the *outgoing* scene (elapsed < duration/2).
    // Phase-2 transitions use this to know whether they should be capturing
    // the live frame to a RT (true) or sampling the captured RT back over
    // the incoming scene (false).
    bool IsBeforeSwap() const;
    // 0..1 progress across the entire duration. Useful for Pixelate's grid
    // size and Crossfade's blend alpha.
    float TransitionProgress() const;
    // Black-overlay alpha for the current transition (0 at the start/end, 1
    // exactly at the halfway swap). Returns 0 when no transition is active.
    float FadeAlpha() const;
    // Normalised (x, y, w, h) inside the screen rect of the Wipe overlay,
    // computed from the elapsed/duration ratio + WipeDirection. Returns
    // (0, 0, 0, 0) for non-Wipe transitions.
    Vec4 WipeOverlayRectNorm() const;

private:
    enum class Action { Push, Pop, Change, Clear };
    struct Pending {
        Action action;
        std::unique_ptr<Scene> scene;
    };
    struct Transition {
        bool                   active   = false;
        bool                   swapped  = false;
        float                  elapsed  = 0.0f;
        float                  duration = 0.0f;
        std::unique_ptr<Scene> next;
        TransitionKind         kind      = TransitionKind::None;
        Color                  color     = Color::Black();
        WipeDirection          direction = WipeDirection::LeftToRight;
    };

    void ApplyPending();
    void DoPush(std::unique_ptr<Scene> scene);
    void DoPop();
    void DoClear();

    std::vector<std::unique_ptr<Scene>> m_stack;
    std::vector<Pending> m_pending;
    Transition           m_transition;
};

} // namespace NaraGame
