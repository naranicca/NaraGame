#include "NaraGame/debug/DebugDraw.h"

#include "NaraGame/physics/Physics2D.h"
#include "NaraGame/renderer/MSDFFont.h"
#include "NaraGame/renderer/Renderer2D.h"

#include <cstdio>

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

void FrameStats(Renderer2D& r, const MSDFFont& font,
                float fps, float dt, uint32_t drawCalls) {
    // Black backing panel for readability against any scene.
    constexpr float kPad = 6.0f;
    constexpr float kTextPx = 14.0f;
    constexpr float kLineH = 18.0f;
    constexpr float kPanelW = 168.0f;
    constexpr float kPanelH = 70.0f;
    r.DrawQuad({4.0f, 4.0f}, {kPanelW, kPanelH}, Color(0.0f, 0.0f, 0.0f, 0.55f));

    char line[64];
    const Color text(1.0f, 1.0f, 1.0f, 1.0f);
    const float x0 = 4.0f + kPad;
    float y = 4.0f + kPad;
    std::snprintf(line, sizeof(line), "FPS  %.1f", fps);
    r.DrawText(font, line, {x0, y}, text, kTextPx); y += kLineH;
    std::snprintf(line, sizeof(line), "dt   %.2f ms", dt * 1000.0f);
    r.DrawText(font, line, {x0, y}, text, kTextPx); y += kLineH;
    std::snprintf(line, sizeof(line), "draws %u", drawCalls);
    r.DrawText(font, line, {x0, y}, text, kTextPx);
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
