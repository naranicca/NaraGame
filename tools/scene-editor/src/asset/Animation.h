#pragma once

#include "AssetGuid.h"

#include <NaraGame/assets/MiniJson.h>

#include <string>
#include <vector>

namespace SceneEditor {

// One step of a sprite animation. The frame names a sub-sprite inside
// the source texture's sprite sheet (TextureImportSettings::spriteSheet)
// and how long that frame stays on screen.
struct AnimationFrame {
    std::string sprite;            // sub-sprite name
    float       duration = 0.1f;   // seconds
};

// One-shot marker fired when playback crosses `time`. The editor logs
// these to the Console panel; runtime callers (Lua hook -- next
// milestone) will dispatch them as scripted events.
struct AnimationEvent {
    float       time = 0.0f;       // seconds from animation start
    std::string name;
};

// .nanim asset: an ordered list of frames sampled from a single source
// texture's sprite sheet. The editor's AnimatorComponent owns playback
// state (time + isPlaying) and points at one of these assets.
class AnimationAsset {
public:
    AssetGuid                     texture;     // source texture asset GUID
    std::vector<AnimationFrame>   frames;
    std::vector<AnimationEvent>   events;
    bool                          loop = true;

    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    void FromJson(const NaraGame::MiniJson::Value& root);
    NaraGame::MiniJson::Value ToJson() const;

    float TotalDuration() const;
    // Return a pointer into `frames` for the frame active at time `t`.
    // Honours the `loop` flag for wrap-around; non-looping animations
    // clamp to the last frame after the total duration. Returns
    // nullptr when frames is empty.
    const AnimationFrame* Sample(float t) const;
};

} // namespace SceneEditor
