#pragma once

#include "NaraGame/math/Vec2.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

#include <cstddef>
#include <random>
#include <vector>

namespace NaraGame {

class Renderer2D;

// Parameters for one burst of particles.
struct ParticleBurst {
    Vec2 position;
    int count = 16;
    float minSpeed = 40.0f;
    float maxSpeed = 160.0f;
    float minLife = 0.3f;
    float maxLife = 0.8f;
    float startSize = 6.0f;
    float endSize = 0.0f;                          // shrink to nothing by default
    Color startColor = Color::White();
    Color endColor = Color(1.0f, 1.0f, 1.0f, 0.0f); // fade to transparent
    Vec2 gravity = {0.0f, 0.0f};                   // constant acceleration
    float dirAngle = 0.0f;                          // base direction, radians (0 = +x)
    float spread = 6.2831853f;                      // angular spread, radians (full circle)
};

// Fixed-capacity pooled 2D particle system. Particles fade and shrink over their
// lifetime and are drawn as colored quads via Renderer2D. The pool recycles
// round-robin, so old particles are reused once capacity is reached.
class ParticleSystem {
public:
    explicit ParticleSystem(size_t maxParticles = 1024);

    void Emit(const ParticleBurst& burst);
    void Update(float dt);
    void Draw(Renderer2D& renderer) const;
    void Clear();

    size_t AliveCount() const;

private:
    struct Particle {
        Vec2 pos;
        Vec2 vel;
        Vec2 accel;
        float age = 0.0f;
        float life = 0.0f;
        float startSize = 0.0f;
        float endSize = 0.0f;
        Color startColor;
        Color endColor;
        bool alive = false;
    };

    std::vector<Particle> m_particles;
    size_t m_next = 0;
    std::mt19937 m_rng;
};

} // namespace NaraGame
