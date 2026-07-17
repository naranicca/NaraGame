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
    // M7c10 -- richer collision response.
    // `mass` controls how much each body absorbs during the
    // push-out + how the impulse splits. Two equal-mass bodies
    // share the MTV 50/50 (matches M7c5 behaviour); a 1-kg ball
    // hitting a 10-kg crate gives the ball 10/11 of the push.
    // mass = 0 is invalid -- the Component falls back to 1.0 to
    // avoid divide-by-zero in TickColliders. Mark a body
    // "infinite mass" by removing the Rigidbody2D entirely
    // (M7c7 already treats no-Rigidbody = static).
    // `restitution` is the bounciness in [0, 1]. 0 absorbs the
    // inward velocity (M7c7 default); 1 reflects it 1:1; 0.5 is
    // a typical rubber ball.
    // `friction` is the per-collision tangential damping in
    // [0, 1]. 0 lets the body slide freely along the surface;
    // 1 stops tangential motion the instant contact lands.
    float          mass        = 1.0f;
    float          restitution = 0.0f;
    float          friction    = 0.0f;

    static const char* StaticTypeName() { return "Rigidbody2D"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
