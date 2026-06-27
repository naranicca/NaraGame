#pragma once

#include "../asset/AssetGuid.h"
#include "Component.h"

#include <NaraGame/assets/MiniJson.h>

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

    // Optional .ncontroller reference. When set, the runtime ignores
    // `animation` and plays the controller's currentState animation
    // instead. paramValues drives the controller's transitions; the
    // editor's inspector exposes a toggle for each parameter.
    AssetGuid controller;
    int       currentState = -1;   // -1 = "not initialised yet"
    std::unordered_map<std::string, bool> paramValues;

    static const char* StaticTypeName() { return "Animator"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
