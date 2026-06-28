#pragma once

#include "Component.h"

#include <NaraGame/assets/MiniJson.h>

namespace SceneEditor {

// M8c8 audio listener marker. Attach to whichever entity
// represents the player's "ears" -- typically the
// camera, the player character, or a follow-target. The
// runtime takes the listener's Transform position as the
// reference point for 3D source attenuation + stereo pan.
//
// Only one listener is active per scene -- the first
// enabled entity carrying the component wins. The
// component itself stores no fields; future revisions can
// add forward / up vectors when we wire X3DAudio for
// real azimuth-based panning.
class AudioListenerComponent final : public Component {
public:
    static const char* StaticTypeName() { return "AudioListener"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& /*out*/) const override {}
    void Deserialize(const NaraGame::MiniJson::Value& /*obj*/) override {}
};

} // namespace SceneEditor
