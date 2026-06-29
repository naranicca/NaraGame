#pragma once

#include "Component.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>

namespace SceneEditor {

// 2D axis-aligned box collider. The runtime overlap pass
// (EditorApp::TickColliders) treats `size` as world-unit
// extents (full width / height, not half-extents) centred on
// the owning Transform's position plus `offset`.
//
// `isTrigger` defaults true so a freshly-dropped BoxCollider
// fires OnTriggerEnter / Exit without pushing entities around
// -- the most common case for level-design probes. Set it to
// false (in the inspector or via deserialisation) to get the
// M7c10 solid response: MTV push-out split by inverse mass +
// bounce by combined restitution + tangent damping by combined
// friction. Without a Rigidbody2D the entity is treated as
// infinite-mass / static, so the moving side absorbs the full
// push.
class BoxColliderComponent final : public Component {
public:
    NaraGame::Vec2 size   { 1.0f, 1.0f };
    NaraGame::Vec2 offset { 0.0f, 0.0f };
    bool           isTrigger = true;
    // M7c11: project-level collision layer. The runtime checks
    // Project::CollisionLayersCollide(this.layer, other.layer)
    // before any overlap test. Defaults to "Default" so legacy
    // scenes keep colliding as before.
    std::string    layer = "Default";

    static const char* StaticTypeName() { return "BoxCollider"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
