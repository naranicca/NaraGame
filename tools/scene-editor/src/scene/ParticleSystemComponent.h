#pragma once

#include "../asset/AssetGuid.h"
#include "Component.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/rhi/RHITypes.h>

#include <cstdint>
#include <string>
#include <vector>

namespace SceneEditor {

// M9c2 2D particle emitter. Entity-attached system that spawns
// short-lived particles each frame at the entity's Transform
// position, integrates them under gravity, and renders each
// as an axis-aligned coloured quad in the viewport's draw
// list. Designed for fire / smoke / dust / muzzle-flash bursts;
// textured particles + custom sub-sprites are future-work.
//
// Per-axis "min/max" ranges (lifetime, speed, size) sample a
// uniform random value per emit so a single set of fields can
// describe both tight beams (min == max) and wide bursts.
// Direction is an angle range in degrees -- 0 = +X axis, CCW
// positive. The runtime stores a fixed-size particle pool
// (size = maxParticles) so steady-state emission costs no
// allocation past the first frame.
class ParticleSystemComponent final : public Component {
public:
    // Pool sizing + emission cadence.
    int   maxParticles  = 200;
    float emissionRate  = 30.0f;   // particles / second

    // Per-particle lifetime + initial speed (sampled per emit).
    float lifetimeMin   = 0.8f;
    float lifetimeMax   = 1.4f;
    float startSpeedMin = 1.0f;
    float startSpeedMax = 2.5f;
    float startSizeMin  = 0.08f;
    float startSizeMax  = 0.18f;

    // Emission direction (degrees, 0 = +X, CCW positive).
    // The default (45 .. 135) shoots upward in a fan; setting
    // min == max + 90 makes a tight column, min = 0 max = 360
    // makes an omnidirectional burst.
    float dirAngleMinDeg = 45.0f;
    float dirAngleMaxDeg = 135.0f;

    // Tinting over the particle's life. The runtime lerps
    // RGBA from startColor at age 0 to endColor at age =
    // lifetime, so fading to alpha = 0 gives a clean tail-out.
    NaraGame::Color startColor = NaraGame::Color::White();
    NaraGame::Color endColor   = NaraGame::Color{1.0f, 1.0f, 1.0f, 0.0f};

    // Constant world-space gravity (units / sec^2). Negative
    // Y is the default falling direction the rest of the
    // engine uses (Y+ is up).
    NaraGame::Vec2 gravity { 0.0f, -2.0f };

    // M9c4 optional texture. Invalid GUID (the default) keeps
    // the M9c2 untextured-rect path; a valid Texture asset
    // makes the renderer route through AddImageQuad. The whole
    // texture is tinted by the lerped colour the same way the
    // rect path is, so an alpha-channel PNG of a smoke puff
    // plus alpha-fading endColor gives a clean soft particle.
    AssetGuid   texture;

    // M9c5 sub-sprite selection. Empty (the default) maps the
    // whole texture onto each particle quad. A non-empty name
    // resolves through the texture's importSettings.spriteSheet
    // at draw time -- same lookup the SpriteRenderer does --
    // so a single spritesheet PNG can drive multiple emitters
    // pointing at different cells (sparkle frames, soot
    // chunks, leaf shapes). Names that don't match any
    // sub-sprite silently fall back to the whole texture
    // rather than going invisible.
    std::string subSprite;

    // Lifecycle flags. `playOnStart` mirrors the M8c7 audio
    // source pattern: rising-edge auto-play when the editor
    // enters play mode. `loop = false` lets the emitter run
    // for one batch (= maxParticles emits) and then stop.
    bool playOnStart = true;
    bool loop        = true;

    // -----------------------------------------------------
    // Runtime state -- not serialized. Re-initialised by
    // Deserialize so re-loading a scene doesn't carry over
    // a stale pool from the previous scene's instance.
    // -----------------------------------------------------
    struct Particle {
        NaraGame::Vec2 pos { 0.0f, 0.0f };
        NaraGame::Vec2 vel { 0.0f, 0.0f };
        float age      = 0.0f;
        float lifetime = 1.0f;
        float size     = 0.1f;
    };
    std::vector<Particle> _pool;          // sized to maxParticles lazily
    int                   _liveCount    = 0;
    float                 _emitAccum    = 0.0f;
    int                   _emittedTotal = 0; // for non-loop early-stop
    bool                  _started      = false;
    // 32-bit xorshift PRNG state -- seeded per-instance on first
    // emit so two particle systems in the same scene don't
    // emit identical sequences.
    std::uint32_t         _rng          = 0;

    static const char* StaticTypeName() { return "ParticleSystem"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
