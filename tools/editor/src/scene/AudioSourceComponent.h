#pragma once

#include "Component.h"

#include "../asset/AssetGuid.h"

#include <NaraGame/assets/MiniJson.h>

namespace SceneEditor {

// Entity-attached sound emitter (M8c7). On the rising edge
// of Play (m_animPlaying false -> true) each enabled source
// with playOnStart fires its clip through Audio::Play (SFX
// cache) or Audio::PlayMusic (streaming) depending on the
// loop flag. The component itself stores configuration
// only; the runtime tracks the "already auto-fired this
// play session" bit in `_started` to make sure a single
// play-mode press doesn't restart the clip every frame.
//
// Use this for ambient sound, BGM that follows a scene,
// looping environment loops, one-shot SFX on spawn.
// Script-driven SFX (footsteps, gunfire) should still
// call audio.play directly from Lua; AudioSource is for
// declarative auto-play.
class AudioSourceComponent final : public Component {
public:
    AssetGuid    clip;
    float        volume      = 1.0f;
    bool         playOnStart = true;
    // When true the runtime routes through PlayMusic
    // (streaming + loop=true). When false the SFX cache
    // path runs; loop is ignored in that case since the
    // M8c2 SFX path is one-shot.
    bool         loop        = false;

    // M8c8 3D positional audio. When `is3D` is true the
    // runtime measures the distance + horizontal offset
    // between this entity's Transform + the active
    // AudioListenerComponent's Transform, attenuates the
    // volume linearly across [minDistance, maxDistance],
    // and pans the mono source toward the side it sits on
    // relative to the listener. Stereo source files keep
    // the attenuation but skip the pan -- they already
    // carry their own L/R balance.
    //
    // 3D + loop is ignored on purpose: streaming voices
    // play once + don't update as the entity moves; a
    // follow-up commit can wire X3DAudio if "music that
    // tracks a 3D object" shows up as a real use case.
    bool         is3D         = false;
    float        minDistance  = 1.0f;
    float        maxDistance  = 10.0f;

    // Runtime-only -- not serialized. Reset on every
    // play-mode entry so a single Play press triggers
    // exactly one auto-fire.
    bool         _started    = false;

    static const char* StaticTypeName() { return "AudioSource"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
