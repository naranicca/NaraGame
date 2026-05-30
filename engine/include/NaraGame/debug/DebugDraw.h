#pragma once

#include "NaraGame/math/Vec2.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

namespace NaraGame {

class Renderer2D;
class PhysicsWorld;

// Debug overlay helpers. Each function draws into a Renderer2D batch the
// caller is already in -- they don't manage Begin/End. Colours follow a
// fixed convention so the same overlay reads the same across every game:
//
//   yellow  = static bodies (walls, tiles)
//   magenta = kinematic bodies (paddles, platforms)
//   cyan    = dynamic bodies (player, enemies, projectiles)
//   sensors render as outline-only in their type colour
namespace DebugDraw {

// Render every active body's AABB. Solid bodies are drawn as a semi-
// transparent fill plus a 1-px outline; sensors are outline-only.
void PhysicsBodies(Renderer2D& renderer, const PhysicsWorld& world);

// One-pixel-thick rectangle outline made of four axis-aligned quads.
// `thickness` is in world units, so it scales with the camera.
void Outline(Renderer2D& renderer, const Vec2& position, const Vec2& size,
             const Color& color, float thickness = 1.0f);

} // namespace DebugDraw
} // namespace NaraGame
