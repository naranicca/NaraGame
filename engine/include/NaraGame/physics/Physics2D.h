#pragma once

#include "NaraGame/math/Vec2.h"

#include <cstdint>
#include <vector>

namespace NaraGame {

enum class BodyType {
    Static,    // never moves (walls, bricks, tiles)
    Kinematic, // moved by game code; blocks dynamics; unaffected by gravity (paddles, platforms)
    Dynamic    // integrated by the world (gravity), resolved against static/kinematic bodies
};

struct Body {
    Vec2 position;             // top-left (min corner), world units
    Vec2 size;
    Vec2 velocity;
    BodyType type = BodyType::Dynamic;
    float restitution = 0.0f;  // bounciness along the contact normal, [0, 1]
    float gravityScale = 1.0f;
    bool sensor = false;       // overlaps are reported but cause no physical response
    bool onGround = false;     // set by Step(): resting on a surface this frame
    uint32_t tag = 0;          // game-defined category
    int id = -1;               // assigned by the world
    bool active = true;
};

struct Contact {
    int a = -1;   // the dynamic body
    int b = -1;   // the other body
    Vec2 normal;  // unit surface normal pushing `a` away from `b` ({0,0} for sensors)
};

// AABB-only 2D physics. Dynamic bodies integrate under gravity and resolve
// against static/kinematic bodies using swept AABB (so fast bodies never tunnel
// through thin ones), with optional restitution. Sensors report overlaps only.
class PhysicsWorld {
public:
    void SetGravity(const Vec2& gravity) { m_gravity = gravity; }
    const Vec2& Gravity() const { return m_gravity; }

    int AddBody(const Body& body); // returns a stable id (index)
    void RemoveBody(int id);
    // Deactivate every body whose `tag` matches. Used when the game wants to
    // wipe a whole category at once (e.g. all tile colliders after a brick
    // break) instead of tracking individual ids.
    void RemoveBodiesByTag(uint32_t tag);
    Body* Get(int id);
    // Read-only access to the full body pool. Debug tools and gameplay code
    // that needs to scan everything use this; gameplay should generally find
    // bodies by id or via the contacts list instead.
    const std::vector<Body>& AllBodies() const { return m_bodies; }
    void Clear();

    void Step(float dt);
    const std::vector<Contact>& Contacts() const { return m_contacts; }

private:
    void ResolveDynamic(Body& body, float dt);
    int SweepAxis(Body& body, const Vec2& delta, Vec2& outNormal);

    Vec2 m_gravity;
    std::vector<Body> m_bodies;
    std::vector<Contact> m_contacts;
};

} // namespace NaraGame
