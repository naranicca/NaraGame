#include "NaraGame/physics/Physics2D.h"

#include <limits>

namespace NaraGame {
namespace {

bool Overlap(const Vec2& aMin, const Vec2& aMax, const Vec2& bMin, const Vec2& bMax) {
    return aMin.x < bMax.x && aMax.x > bMin.x && aMin.y < bMax.y && aMax.y > bMin.y;
}

// Swept AABB: box A (aMin..aMax) moving by `delta` against static box B. Returns
// the entry time in [0, 1] (1 = no hit) and the surface normal. Classic
// slab/Minkowski sweep.
float Swept(const Vec2& aMin, const Vec2& aMax, const Vec2& delta, const Vec2& bMin,
            const Vec2& bMax, Vec2& normal) {
    const float inf = std::numeric_limits<float>::infinity();

    float xInvEntry, xInvExit, yInvEntry, yInvExit;
    if (delta.x > 0.0f) { xInvEntry = bMin.x - aMax.x; xInvExit = bMax.x - aMin.x; }
    else { xInvEntry = bMax.x - aMin.x; xInvExit = bMin.x - aMax.x; }
    if (delta.y > 0.0f) { yInvEntry = bMin.y - aMax.y; yInvExit = bMax.y - aMin.y; }
    else { yInvEntry = bMax.y - aMin.y; yInvExit = bMin.y - aMax.y; }

    float xEntry, xExit, yEntry, yExit;
    if (delta.x == 0.0f) { xEntry = -inf; xExit = inf; }
    else { xEntry = xInvEntry / delta.x; xExit = xInvExit / delta.x; }
    if (delta.y == 0.0f) { yEntry = -inf; yExit = inf; }
    else { yEntry = yInvEntry / delta.y; yExit = yInvExit / delta.y; }

    const float entry = (xEntry > yEntry) ? xEntry : yEntry;
    const float exit = (xExit < yExit) ? xExit : yExit;

    normal = {0.0f, 0.0f};
    if (entry > exit || (xEntry < 0.0f && yEntry < 0.0f) || xEntry > 1.0f || yEntry > 1.0f)
        return 1.0f;

    if (xEntry > yEntry)
        normal = {xInvEntry < 0.0f ? 1.0f : -1.0f, 0.0f};
    else
        normal = {0.0f, yInvEntry < 0.0f ? 1.0f : -1.0f};
    return entry < 0.0f ? 0.0f : entry;
}

constexpr float kSkin = 0.01f; // small separation to avoid sticking at contact

} // namespace

int PhysicsWorld::AddBody(const Body& body) {
    Body b = body;
    b.id = static_cast<int>(m_bodies.size());
    b.active = true;
    m_bodies.push_back(b);
    return b.id;
}

void PhysicsWorld::RemoveBody(int id) {
    if (id >= 0 && id < static_cast<int>(m_bodies.size()))
        m_bodies[static_cast<size_t>(id)].active = false;
}

Body* PhysicsWorld::Get(int id) {
    if (id >= 0 && id < static_cast<int>(m_bodies.size()))
        return &m_bodies[static_cast<size_t>(id)];
    return nullptr;
}

void PhysicsWorld::Clear() {
    m_bodies.clear();
    m_contacts.clear();
}

void PhysicsWorld::ResolveDynamic(Body& b, float dt) {
    b.onGround = false;
    Vec2 delta = b.velocity * dt;

    for (int iter = 0; iter < 4; ++iter) {
        if (LengthSq(delta) < 1e-8f)
            break;

        const Vec2 aMin = b.position;
        const Vec2 aMax = b.position + b.size;
        float bestT = 1.0f;
        Vec2 bestN{0.0f, 0.0f};
        int bestId = -1;
        for (Body& o : m_bodies) {
            if (!o.active || o.id == b.id || o.type == BodyType::Dynamic || o.sensor)
                continue;
            Vec2 n;
            const float t = Swept(aMin, aMax, delta, o.position, o.position + o.size, n);
            if (t < bestT) { bestT = t; bestN = n; bestId = o.id; }
        }

        b.position += delta * bestT;
        if (bestId < 0)
            break; // travelled the full delta with no hit

        b.position += bestN * kSkin; // nudge off the surface
        m_contacts.push_back(Contact{b.id, bestId, bestN});
        if (bestN.y < 0.0f)
            b.onGround = true;

        const float e = b.restitution;
        const Vec2 remaining = delta * (1.0f - bestT);
        b.velocity = b.velocity - bestN * ((1.0f + e) * Dot(b.velocity, bestN));
        delta = remaining - bestN * ((1.0f + e) * Dot(remaining, bestN));
    }
}

void PhysicsWorld::Step(float dt) {
    m_contacts.clear();

    for (Body& b : m_bodies) {
        if (!b.active || b.type != BodyType::Dynamic)
            continue;
        b.velocity += m_gravity * (b.gravityScale * dt);
        ResolveDynamic(b, dt);
    }

    // Sensor overlaps (no physical response).
    for (Body& d : m_bodies) {
        if (!d.active || d.type != BodyType::Dynamic)
            continue;
        const Vec2 dMin = d.position;
        const Vec2 dMax = d.position + d.size;
        for (Body& s : m_bodies) {
            if (!s.active || !s.sensor || s.id == d.id)
                continue;
            if (Overlap(dMin, dMax, s.position, s.position + s.size))
                m_contacts.push_back(Contact{d.id, s.id, Vec2{0.0f, 0.0f}});
        }
    }
}

} // namespace NaraGame
