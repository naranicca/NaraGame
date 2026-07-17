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

// M9c10 emission shape. Point (the default) spawns every
// particle at the emitter's Transform position; the other
// shapes pick an offset from a 2D area / curve so multi-
// particle systems can come from a wider source than a
// single dot. Edge variants sample the boundary; Volume
// variants sample the interior uniformly. Line samples a
// segment from the emitter origin to `shapeLineEnd` in
// local coordinates.
enum class ParticleEmissionShape {
    Point        = 0,
    CircleEdge   = 1,
    CircleVolume = 2,
    BoxEdge      = 3,
    BoxVolume    = 4,
    Line         = 5,
};

// M9c2 2D particle emitter. Entity-attached system that spawns
// short-lived particles each frame at the entity's Transform
// position, integrates them under gravity, and renders each
// as a coloured / textured quad in the viewport's draw list.
// Designed for fire / smoke / dust / muzzle-flash bursts.
// M9c4 added textured particles; M9c5 sub-sprite support;
// M9c6 per-particle frame animation; M9c7 rotation +
// velocity-align; M9c8 size-over-lifetime; M9c9 burst
// events; M9c10 emission shapes.
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
    // M9c8 end-of-life size. Sampled per emit independently
    // of startSize so a particle that spawns at 0.1 can end
    // at 0.4 (grow) or 0.0 (shrink to nothing). The renderer
    // lerps `startSize -> endSize` across age/lifetime. The
    // sentinel default of -1 means "use startSize as endSize"
    // -- legacy scenes without these fields load that way so
    // size stays constant through the lifetime the same as
    // pre-M9c8 behaviour. Inspector + serialize translate the
    // sentinel; user-visible values are always real sizes.
    float endSizeMin    = -1.0f;
    float endSizeMax    = -1.0f;

    // Emission direction (degrees, 0 = +X, CCW positive).
    // The default (45 .. 135) shoots upward in a fan; setting
    // min == max + 90 makes a tight column, min = 0 max = 360
    // makes an omnidirectional burst.
    float dirAngleMinDeg = 45.0f;
    float dirAngleMaxDeg = 135.0f;

    // M9c7 rotation. Initial rotation (degrees, CCW around the
    // quad's centre) is sampled per emit from [startRotationMin,
    // startRotationMax]. Angular speed (degrees/sec) is sampled
    // the same way + integrated into the particle's rotation
    // every tick. `velocityAlign = true` overrides both -- the
    // renderer instead rotates the quad to match each particle's
    // current velocity direction (typical for sparks / debris
    // that should streak in the direction of travel).
    float startRotationMinDeg = 0.0f;
    float startRotationMaxDeg = 0.0f;
    float angularSpeedMinDeg  = 0.0f;
    float angularSpeedMaxDeg  = 0.0f;
    bool  velocityAlign       = false;

    // Tinting over the particle's life. The runtime lerps
    // RGBA from startColor at age 0 to endColor at age =
    // lifetime, so fading to alpha = 0 gives a clean tail-out.
    NaraGame::Color startColor = NaraGame::Color::White();
    NaraGame::Color endColor   = NaraGame::Color{1.0f, 1.0f, 1.0f, 0.0f};

    // Constant world-space gravity (units / sec^2). Negative
    // Y is the default falling direction the rest of the
    // engine uses (Y+ is up).
    NaraGame::Vec2 gravity { 0.0f, -2.0f };

    // M9c10 emission shape + per-shape size params. Point
    // ignores all three size fields; Circle uses `shapeRadius`;
    // Box uses `shapeBoxSize` (full width / height); Line uses
    // `shapeLineEnd` as the offset of the segment's far end
    // from the entity origin (the near end is always 0,0).
    // All three are stored even when not in use so swapping
    // shapes doesn't lose the user's previous values.
    ParticleEmissionShape shape         = ParticleEmissionShape::Point;
    float                 shapeRadius   = 1.0f;
    NaraGame::Vec2        shapeBoxSize  { 1.0f, 1.0f };
    NaraGame::Vec2        shapeLineEnd  { 1.0f, 0.0f };

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

    // M9c6 per-particle frame animation. A non-empty `animFrames`
    // list overrides `subSprite` -- the renderer cycles each
    // particle through the listed sub-sprites at `animFps` frames
    // per second, indexed against the particle's own `age` so
    // every particle's animation starts at frame 0 when it's
    // spawned. `animLoop = true` wraps via modulo; false clamps
    // at the last frame for the rest of the particle's lifetime
    // (useful for one-shot explosion / impact sequences).
    //
    // Empty list = use the M9c5 single-sub-sprite path. UV
    // resolution is cached per emitter per frame (one sheet
    // walk per frame, not per particle) so a 200-particle
    // emitter on an 8-frame sprite costs 8 lookups + 200
    // indexings per draw.
    std::vector<std::string> animFrames;
    float                    animFps  = 8.0f;
    bool                     animLoop = true;

    // Lifecycle flags. `playOnStart` mirrors the M8c7 audio
    // source pattern: rising-edge auto-play when the editor
    // enters play mode. `loop = false` lets the emitter run
    // for one batch (= maxParticles emits or one burst cycle)
    // and then stop; `loop = true` wraps the burst cycle
    // every `duration` seconds.
    bool playOnStart = true;
    bool loop        = true;

    // M9c9 timed burst events. Each entry fires `count`
    // particles when the emitter's internal elapsed time
    // first crosses `time` (seconds since the cycle began).
    // Bursts compose on top of the per-second
    // `emissionRate` -- the continuous stream + the burst
    // hits both feed into the same live pool, capped at
    // `maxParticles`. The list is fired in user-given order;
    // sort by time in the inspector for predictable behaviour.
    struct Burst {
        float time  = 0.0f;
        int   count = 10;
    };
    // Cycle length in seconds. When `loop = true` the
    // emitter resets its elapsed timer + the next-burst
    // cursor every `duration` seconds, so bursts re-fire.
    float              duration = 3.0f;
    std::vector<Burst> bursts;

    // -----------------------------------------------------
    // Runtime state -- not serialized. Re-initialised by
    // Deserialize so re-loading a scene doesn't carry over
    // a stale pool from the previous scene's instance.
    // -----------------------------------------------------
    struct Particle {
        NaraGame::Vec2 pos { 0.0f, 0.0f };
        NaraGame::Vec2 vel { 0.0f, 0.0f };
        float age        = 0.0f;
        float lifetime   = 1.0f;
        float size       = 0.1f;
        // M9c8 end-of-life size. Sampled once per emit so
        // the lerp is deterministic across the particle's
        // life even though the component's endSizeMin/Max can
        // change underfoot during play-mode tweaking.
        float endSize    = 0.1f;
        // M9c7. Stored in radians so the draw path's
        // cos/sin doesn't have to convert every frame.
        // `angularVel` is also radians/sec; both are
        // ignored when the emitter's velocityAlign flag is on.
        float rotation   = 0.0f;
        float angularVel = 0.0f;
    };
    std::vector<Particle> _pool;          // sized to maxParticles lazily
    int                   _liveCount    = 0;
    float                 _emitAccum    = 0.0f;
    int                   _emittedTotal = 0; // for non-loop early-stop
    bool                  _started      = false;
    // M9c9 burst-cycle state. _elapsed is the time inside the
    // current cycle (0 .. duration); _burstNextIndex points
    // at the next burst in `bursts` that hasn't fired yet
    // this cycle. Loop wrap resets both.
    float                 _elapsed         = 0.0f;
    int                   _burstNextIndex  = 0;
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
