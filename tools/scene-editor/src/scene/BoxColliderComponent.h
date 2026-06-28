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
// `isTrigger` defaults true so M7c1 ships an "overlap only,
// no physics response" surface -- scripts react via the
// OnTriggerEnter / OnTriggerExit Lua hooks. A future commit
// can add solid collisions on top of the same data model.
class BoxColliderComponent final : public Component {
public:
    NaraGame::Vec2 size   { 1.0f, 1.0f };
    NaraGame::Vec2 offset { 0.0f, 0.0f };
    bool           isTrigger = true;

    static const char* StaticTypeName() { return "BoxCollider"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
