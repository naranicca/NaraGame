#pragma once

#include "Component.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>

namespace SceneEditor {

// 2D capsule collider. The spine runs along the owning
// Transform's local Y axis from (0, -halfLength) to
// (0, +halfLength); each endpoint carries a hemispherical
// cap of `radius`. Total local-space height = 2 *
// (halfLength + radius); width = 2 * radius.
//
// Useful for upright characters (player, NPC, enemy) where
// a Box collider's flat corners would snag on slopes and a
// Circle collider would be too wide at the feet. M7c27 ships
// the data model + a decomposition-based collision path
// (rect core + 2 endcap circles); a follow-up commit can
// replace the decomposition with a true segment-vs-shape
// SAT if the rounded transition shows up as a visible bump.
class CapsuleColliderComponent final : public Component {
public:
    float          radius     = 0.25f;
    float          halfLength = 0.25f;
    NaraGame::Vec2 offset { 0.0f, 0.0f };
    bool           isTrigger  = false;
    // M7c11-style collision layer; flows through the matrix
    // the same way Box / Circle do.
    std::string    layer = "Default";

    static const char* StaticTypeName() { return "CapsuleCollider"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
