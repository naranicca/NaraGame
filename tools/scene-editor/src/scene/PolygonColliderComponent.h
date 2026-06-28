#pragma once

#include "Component.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>

#include <vector>

namespace SceneEditor {

// 2D convex polygon collider. The vertex list is stored in
// the owning Transform's local frame (CCW order); rotation
// + scale flow through Transform the same way Box does. The
// runtime synthesises a ColliderInfo whose `polygonVerts`
// array holds the world-space corners, and the existing
// n-gon SAT (M7c25 SatPolyBox + PolyCircleClosest) does the
// heavy lifting -- the SAT was already general enough; M7c28
// just feeds it more vertex counts.
//
// Use this for game-specific shapes that don't fit the
// Box / Circle / Capsule presets: pentagons, hexagons,
// shields, custom hitboxes. Up to 16 vertices fit in the
// inspector grid before things get unwieldy; the collision
// path itself imposes no upper bound aside from the n-gon
// SAT's O(n) per axis.
class PolygonColliderComponent final : public Component {
public:
    // Default = regular pentagon, radius 0.4. Constructor fills
    // it so a freshly-added component is immediately useful in
    // the editor.
    PolygonColliderComponent();

    std::vector<NaraGame::Vec2> vertices;
    NaraGame::Vec2 offset { 0.0f, 0.0f };
    bool           isTrigger = false;
    std::string    layer     = "Default";

    static const char* StaticTypeName() { return "PolygonCollider"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
