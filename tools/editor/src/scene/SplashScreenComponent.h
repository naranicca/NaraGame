#pragma once

#include "../asset/AssetGuid.h"
#include "Component.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/renderer/rhi/RHITypes.h>

#include <string>

namespace SceneEditor {

// M10c2 splash / intro screen overlay. An entity carrying this
// component takes over the standalone viewport for a configurable
// duration, fading a fullscreen texture in -> holding -> fading out
// + then deferring to `nextScene` (typed the same way Lua's
// scene.load expects: relative path or asset GUID stringification).
//
// The runtime walks the scene each frame, finds the highest-
// priority enabled splash component, + draws it on top of the
// viewport's normal contents. Useful for studio logos / publisher
// idents / "press start" attract screens without writing a Lua
// timer + scene.load yourself. Multiple splash components on
// different entities chain by leaving each one's nextScene
// pointing at the scene that holds the next splash.
//
// Timeline is: t in [0, fadeIn) -> alpha lerps 0 -> 1
//              t in [fadeIn, fadeIn+hold) -> alpha = 1
//              t in [fadeIn+hold, fadeIn+hold+fadeOut) -> alpha lerps 1 -> 0
//              t >= total -> nextScene fires once via
//                            ScriptHost::SetPendingSceneLoad
//
// When nextScene is empty the component completes (alpha 0) and
// sits there inert; the editor / runtime keeps drawing the scene
// underneath normally. The component is editor-aware: Play mode
// drives the timer in editor; standalone always drives. Toggling
// Play off resets `_elapsed` so the next Play replays from t=0.
class SplashScreenComponent final : public Component {
public:
    // Fullscreen image. When unset (invalid guid) the panel just
    // tints the screen `background` -- still useful as a
    // controlled fade-to-black between scenes.
    AssetGuid       texture;
    // Color the panel fades from / to. Texture alpha is multiplied
    // on top, so a transparent PNG still gets the background tint
    // behind it for a uniform fade.
    NaraGame::Color background = NaraGame::Color::Black();
    // Image tint applied as a vertex color (multiplied with the
    // sampled texel). Default white = no tint. The alpha channel
    // of `tint` is multiplied with the timeline alpha so a script
    // can additionally fade by editing it at runtime.
    NaraGame::Color tint       = NaraGame::Color::White();

    float fadeIn    = 0.5f;  // seconds
    float hold      = 1.5f;  // seconds
    float fadeOut   = 0.5f;  // seconds
    // Path / GUID string handed to ScriptHost::SetPendingSceneLoad
    // when the timeline completes. Empty = do nothing on finish.
    std::string nextScene;
    // Higher wins when multiple splash entities coexist. Lets a
    // "skip-to-main" override sit on top of a longer intro chain.
    int priority = 0;

    // --- transient runtime state ---------------------------------
    float _elapsed     = 0.0f;
    bool  _completed   = false;  // nextScene already fired
    bool  _everPlayed  = false;  // first-frame guard; resets in editor on Play toggle

    static const char* StaticTypeName() { return "SplashScreen"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;

    // Returns alpha in [0,1] for the current `_elapsed`. After the
    // timeline ends the splash is invisible. Pure function -- the
    // runtime caches whatever it needs separately.
    float CurrentAlpha() const {
        const float total = fadeIn + hold + fadeOut;
        if (_elapsed >= total) return 0.0f;
        if (fadeIn > 0.0f && _elapsed < fadeIn) {
            return _elapsed / fadeIn;
        }
        if (_elapsed < fadeIn + hold) return 1.0f;
        if (fadeOut <= 0.0f) return 0.0f;
        const float t = (_elapsed - fadeIn - hold) / fadeOut;
        return 1.0f - t;
    }
    float TotalDuration() const { return fadeIn + hold + fadeOut; }
};

} // namespace SceneEditor
