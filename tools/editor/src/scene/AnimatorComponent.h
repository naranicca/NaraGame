#pragma once

#include "../asset/AssetGuid.h"
#include "Component.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/rhi/RHITypes.h>

#include <string>
#include <unordered_map>

namespace SceneEditor {

// Plays an AnimationAsset against a companion SpriteRendererComponent.
// The editor's playback tick advances `currentTime` and writes the
// sampled frame's sub-sprite name into the renderer; runtime code can
// reuse the same Sample call to drive the render path directly.
//
// M5c1 ships the data + editor-side playback. Trigger / event /
// blend-tree behaviour lives in a follow-up.
class AnimatorComponent final : public Component {
public:
    AssetGuid animation;
    float     currentTime = 0.0f;
    bool      isPlaying   = true;
    // Runtime-only cursor used to detect "we just crossed an event".
    // Not serialised because it follows currentTime each tick and
    // resets to 0 on a state switch.
    float     prevTime    = 0.0f;
    // Transition blend runtime state. -1 = no transition in flight.
    // duration / elapsed track the cross-fade; designAlpha captures
    // the SpriteRenderer's pre-transition alpha so we can restore it
    // cleanly on completion. triggerParam is the trigger to consume
    // when the blend finishes (deferred so the user sees the live
    // "armed" indicator survive the fade).
    int         transitionPending      = -1;
    float       transitionDuration     = 0.0f;
    float       transitionElapsed      = 0.0f;
    bool        transitionSwapped      = false;
    float       transitionDesignAlpha  = 1.0f;
    std::string transitionTriggerParam;

    // Optional .ncontroller reference. When set, the runtime ignores
    // `animation` and plays the controller's currentState animation
    // instead. Live parameter values live in three type-matched maps;
    // the editor's inspector picks the right widget per parameter.
    AssetGuid controller;
    int       currentState = -1;   // -1 = "not initialised yet"
    std::unordered_map<std::string, bool>           paramBools;
    std::unordered_map<std::string, float>          paramFloats;
    std::unordered_map<std::string, int>            paramInts;
    std::unordered_map<std::string, NaraGame::Vec2> paramVec2;
    std::unordered_map<std::string, NaraGame::Color> paramColors;
    std::unordered_map<std::string, std::string>    paramStrings;

    static const char* StaticTypeName() { return "Animator"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
