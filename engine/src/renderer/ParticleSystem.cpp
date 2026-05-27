#include "NaraGame/renderer/ParticleSystem.h"

#include "NaraGame/math/MathUtil.h"
#include "NaraGame/renderer/Renderer2D.h"

#include <cmath>

namespace NaraGame {

ParticleSystem::ParticleSystem(size_t maxParticles)
    : m_particles(maxParticles), m_rng(std::random_device{}()) {}

void ParticleSystem::Emit(const ParticleBurst& burst) {
    if (m_particles.empty())
        return;
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    for (int i = 0; i < burst.count; ++i) {
        Particle& p = m_particles[m_next];
        m_next = (m_next + 1) % m_particles.size();

        const float angle = burst.dirAngle + (u01(m_rng) - 0.5f) * burst.spread;
        const float speed = burst.minSpeed + u01(m_rng) * (burst.maxSpeed - burst.minSpeed);
        p.pos = burst.position;
        p.vel = {std::cos(angle) * speed, std::sin(angle) * speed};
        p.accel = burst.gravity;
        p.life = burst.minLife + u01(m_rng) * (burst.maxLife - burst.minLife);
        p.age = 0.0f;
        p.startSize = burst.startSize;
        p.endSize = burst.endSize;
        p.startColor = burst.startColor;
        p.endColor = burst.endColor;
        p.alive = true;
    }
}

void ParticleSystem::Update(float dt) {
    for (Particle& p : m_particles) {
        if (!p.alive)
            continue;
        p.age += dt;
        if (p.age >= p.life) {
            p.alive = false;
            continue;
        }
        p.vel += p.accel * dt;
        p.pos += p.vel * dt;
    }
}

void ParticleSystem::Draw(Renderer2D& renderer) const {
    for (const Particle& p : m_particles) {
        if (!p.alive)
            continue;
        const float t = (p.life > 0.0f) ? p.age / p.life : 1.0f;
        const float size = Lerp(p.startSize, p.endSize, t);
        if (size <= 0.0f)
            continue;
        const Color color{Lerp(p.startColor.r, p.endColor.r, t),
                          Lerp(p.startColor.g, p.endColor.g, t),
                          Lerp(p.startColor.b, p.endColor.b, t),
                          Lerp(p.startColor.a, p.endColor.a, t)};
        renderer.DrawQuad({p.pos.x - size * 0.5f, p.pos.y - size * 0.5f}, {size, size}, color);
    }
}

void ParticleSystem::Clear() {
    for (Particle& p : m_particles)
        p.alive = false;
    m_next = 0;
}

size_t ParticleSystem::AliveCount() const {
    size_t n = 0;
    for (const Particle& p : m_particles)
        if (p.alive)
            ++n;
    return n;
}

} // namespace NaraGame
