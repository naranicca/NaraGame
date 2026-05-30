#include "NaraGame/debug/DebugDraw.h"

#include "NaraGame/physics/Physics2D.h"
#include "NaraGame/renderer/Renderer2D.h"

namespace NaraGame {
namespace DebugDraw {

void Outline(Renderer2D& r, const Vec2& p, const Vec2& s,
             const Color& color, float t) {
    if (t <= 0.0f) return;
    r.DrawQuad({p.x, p.y},               {s.x, t},       color);   // top
    r.DrawQuad({p.x, p.y + s.y - t},     {s.x, t},       color);   // bottom
    r.DrawQuad({p.x, p.y},               {t,   s.y},     color);   // left
    r.DrawQuad({p.x + s.x - t, p.y},     {t,   s.y},     color);   // right
}

void PhysicsBodies(Renderer2D& r, const PhysicsWorld& world) {
    for (const Body& b : world.AllBodies()) {
        if (!b.active) continue;

        Color fill, edge;
        switch (b.type) {
            case BodyType::Static:
                fill = Color(1.0f, 0.85f, 0.0f, 0.18f);
                edge = Color(1.0f, 0.85f, 0.0f, 0.85f);
                break;
            case BodyType::Kinematic:
                fill = Color(1.0f, 0.30f, 0.95f, 0.18f);
                edge = Color(1.0f, 0.30f, 0.95f, 0.85f);
                break;
            case BodyType::Dynamic:
                fill = Color(0.10f, 0.85f, 1.0f, 0.22f);
                edge = Color(0.10f, 0.85f, 1.0f, 0.90f);
                break;
        }

        if (b.sensor) {
            Outline(r, b.position, b.size, edge, 1.0f);
        } else {
            r.DrawQuad(b.position, b.size, fill);
            Outline(r, b.position, b.size, edge, 1.0f);
        }
    }
}

} // namespace DebugDraw
} // namespace NaraGame
