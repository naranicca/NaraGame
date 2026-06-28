#pragma once

#include "Component.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>

#include <vector>

namespace SceneEditor {

// 2D edge / chain collider. The vertex list defines a poly-
// line in the owning Transform's local frame; each adjacent
// pair contributes one collision segment. `closed = true`
// also adds a segment from the last vertex back to the first
// (terrain ring / arena boundary), otherwise the chain stays
// open (terrain heightmap / sloped floor).
//
// Use this for thin terrain features: floors + ramps + arena
// walls + chain-fence colliders. Box / Circle / Polygon
// bodies pushed against an edge get reflected perpendicular
// to the segment they're touching; the chain has no inside,
// so a body can approach from either side.
//
// M7c32 implements collision via per-segment thin-OBB
// decomposition (thickness = 0.02 world units) routed
// through the existing M7c13 SAT path; a later commit can
// replace this with closed-form segment-vs-shape distance
// if the residual half-thickness shows up as a visible bump.
class EdgeColliderComponent final : public Component {
public:
    EdgeColliderComponent();

    std::vector<NaraGame::Vec2> vertices;
    bool                        closed    = false;
    NaraGame::Vec2              offset { 0.0f, 0.0f };
    bool                        isTrigger = false;
    std::string                 layer     = "Default";

    static const char* StaticTypeName() { return "EdgeCollider"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
