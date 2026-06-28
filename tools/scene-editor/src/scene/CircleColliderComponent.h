#pragma once

#include "Component.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>

namespace SceneEditor {

// 2D circle collider. Center is the owning Transform's
// position + offset; `radius` is in world units. Combines
// freely with BoxColliderComponent through the M7c4 unified
// overlap pass -- circle-vs-circle uses centre distance,
// circle-vs-box uses the nearest-point-on-box test.
class CircleColliderComponent final : public Component {
public:
    float          radius   = 0.5f;
    NaraGame::Vec2 offset { 0.0f, 0.0f };
    bool           isTrigger = true;

    static const char* StaticTypeName() { return "CircleCollider"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
