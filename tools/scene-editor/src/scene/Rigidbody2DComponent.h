#pragma once

#include "Component.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>

namespace SceneEditor {

// 2D rigidbody. Semi-implicit Euler integration each editor
// tick:
//   velocity += gravity * dt
//   velocity *= max(0, 1 - drag * dt)   (per-frame damping)
//   position += velocity * dt
// Plays nicely with the M7c5 push-out path -- the integrator
// runs first + collisions then resolve overlaps without
// touching velocity. Scripts can drive velocity directly
// through rigidbody.set_velocity (kinematic-style) or stack
// gravity / drag on top (dynamic-style).
class Rigidbody2DComponent final : public Component {
public:
    NaraGame::Vec2 velocity { 0.0f, 0.0f };
    NaraGame::Vec2 gravity  { 0.0f, 0.0f };
    float          drag     = 0.0f;

    static const char* StaticTypeName() { return "Rigidbody2D"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
