// Super Mario clone (슈퍼마리오) — scene/state management + file-loaded levels.
//
// Title -> Game <-> Pause flow via a SceneManager stack, two levels loaded from
// text files (level1.txt / level2.txt), platformer physics from the previous
// step. Controls: Enter start, Left/Right or A/D move, Space/Up jump, P pause,
// Esc quit (to title from pause, or close from title).

#include <NaraGame/assets/BMFont.h>
#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/assets/TileLevel.h>
#include <NaraGame/audio/AudioClip.h>
#include <NaraGame/audio/AudioDevice.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/physics/Physics2D.h>
#include <NaraGame/assets/MSDFFontLoader.h>
#include <NaraGame/renderer/MSDFFont.h>
#include <NaraGame/renderer/Camera2D.h>
#include <NaraGame/renderer/Font.h>
#include <NaraGame/renderer/ParticleSystem.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/SpriteSheet.h>
#include <NaraGame/renderer/TextureAtlas.h>
#include <NaraGame/renderer/Tilemap.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>
#include <NaraGame/scene/Scene.h>
#include <NaraGame/scene/SceneManager.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {
constexpr float kScreenW = 1024.0f;
constexpr float kScreenH = 576.0f;
constexpr int kTile = 32;
constexpr float kT = static_cast<float>(kTile);
constexpr int kMaxLevel = 2;
// Camera zoom: the window shows 32 tiles wide at 1:1, which renders everything
// at half the classic-Mario scale. 2x gives the iconic 16-tile view and makes
// the characters/tiles read at a proper size.
constexpr float kZoom = 2.0f;

enum Tile { EMPTY = -1, GROUND = 0, DIRT = 1, BRICK = 2, QUESTION = 3,
            PIPE_TL = 4, PIPE_TR = 5, PIPE_BL = 6, PIPE_BR = 7, HARD = 8, COIN = 9,
            USED = 10 };

// USED is what a ? block becomes after it is bumped — still solid.
bool IsSolidTile(int id) { return (id >= GROUND && id <= HARD) || id == USED; }

// Physics tuned to feel like SMB rather than the looser previous numbers:
// * small Mario is a 1-tile square (kT x kT), matching the new 16x16 NES
//   sprite drawn 2x into a 32x32 atlas cell
// * standing jump clears ~3.5 tiles -- v^2 / 2g = (720^2)/(2*2300) ~ 113 px
// * running adds horizontal speed *and* lifts the jump apex via the gravity
//   carry from forward momentum (we just inherit it through kRunSpeed)
constexpr float kGravity = 2300.0f;
constexpr float kFastFall = 2.1f;
constexpr float kMaxFall = 950.0f;
constexpr float kWalkSpeed = 180.0f;
constexpr float kRunSpeed  = 320.0f;
constexpr float kJumpSpeed    = 780.0f;   // standing jump: ~4 tiles (lands on lone SMB1-1 ? block)
constexpr float kRunJumpBonus = 90.0f;    // at full run, impulse is 870 -> ~5.1 tiles

// SMB-feel locomotion: gradual accel + friction rather than instant velocity.
// Ground accel is brisk so Mario still feels responsive; skid (decel when
// pressing the opposite direction at speed) is fastest so a 180-degree turn
// at full sprint reads as a snappy pivot. Air values are softer so airborne
// horizontal influence is a fine adjustment, not a steering rod.
constexpr float kGroundAccel = 900.0f;
constexpr float kGroundDecel = 700.0f;
constexpr float kSkidDecel   = 1600.0f;
constexpr float kAirAccel    = 650.0f;
constexpr float kAirDecel    = 200.0f;

// Coyote time: even after walking off a ledge, jump still works for this
// many seconds. Jump buffer: press jump up to this far before landing and
// the impulse fires on touchdown. Both pulled straight from modern
// platformer practice -- SMB1 didn't have them but their absence reads as
// "stiff" by modern standards.
constexpr float kCoyoteSeconds     = 0.10f;
constexpr float kJumpBufferSeconds = 0.08f;
constexpr float kStompBounce = 380.0f;
constexpr float kPlayerSmallW = kT;
constexpr float kPlayerSmallH = kT;
constexpr float kPlayerSuperW = kT;
constexpr float kPlayerSuperH = kT * 2.0f;
constexpr float kGoombaW = kT;
constexpr float kGoombaH = kT;
constexpr float kGoombaSpeed = 60.0f;

// Mushroom -- the iconic Super Mushroom power-up. Smaller hitbox than its
// sprite so the pickup feels generous; walks right, falls with gravity,
// reverses on wall hits like a goomba.
constexpr float kMushroomW = kT * 0.85f;
constexpr float kMushroomH = kT * 0.85f;
constexpr float kMushroomSpeed = 90.0f;

// Brief mercy invulnerability after Super Mario takes a hit and shrinks back
// to Small. The HUD flashes for this long so the player has time to retreat.
constexpr float kInvulnSeconds = 1.5f;

// While growing or shrinking, SMB freezes the world for a short beat and
// alternates the Mario sprite between Small and Super forms. The pause is
// short enough that runs still flow but long enough to read.
constexpr float kTransformSeconds = 0.55f;

// Floating "+200" / "+1000" tags rise this far over their lifetime before
// fading. The world-space distance is in screen pixels at zoom 1, so it
// reads at ~half a tile of travel at zoom 2.
constexpr float kScorePopRise   = 28.0f;
constexpr float kScorePopLife   = 0.8f;
constexpr float kCoinPopRise    = 48.0f;
constexpr float kCoinPopLife    = 0.45f;

enum BodyTag : uint32_t { TagTile = 1, TagPlayer = 2, TagGoomba = 3, TagMushroom = 4 };

struct Goomba {
    int bodyId = -1;
    Vec2 lastPos;          // cached for drawing while squashed (body removed)
    float dir = -1.0f;     // -1 left, +1 right
    bool alive = true;
    float squashTimer = 0.0f;
    float animTimer = 0.0f;
    int frame = 0;
};

struct Mushroom {
    int bodyId = -1;
    float dir = 1.0f;      // starts walking right out of the block it sprang from
    bool alive = true;
    float spawnTimer = 0.0f;  // brief rise-out animation before physics kicks in
    Vec2 spawnAt;             // bottom-left of the block it came from
};

// Visual-only: a coin that pops out of a bumped ?-block, rises straight up
// then falls back. Carries no gameplay -- the coin is "given" the instant the
// block is bumped.
struct CoinPop {
    Vec2 origin;  // block-top centre where the coin starts
    float t = 0.0f; // 0..kCoinPopLife
};

// Visual-only: a "+200" style label that floats up and fades.
struct ScorePop {
    Vec2 origin;
    int value = 0;
    float t = 0.0f; // 0..kScorePopLife
};

// Player size mode. Affects hitbox (32x32 vs 32x64), sprite source rect, head
// height (for bumping blocks), and damage handling.
enum class MarioForm { Small, Super };

// Shared resources + scene factories, owned by the Application.
struct GameContext {
    Renderer2D* renderer = nullptr;
    MSDFFont* font = nullptr;   // also serves the old propFont tagline path
    TextureAtlas* chars = nullptr;
    RHITexture* tilesTexture = nullptr;
    SpriteSheet tileset;
    float screenW = kScreenW;
    float screenH = kScreenH;
    std::string assetsDir;

    AudioDevice* audio = nullptr;
    const AudioClip* sfxJump = nullptr;
    const AudioClip* sfxCoin = nullptr;
    const AudioClip* sfxStomp = nullptr;
    const AudioClip* music = nullptr;

    void Sfx(const AudioClip* clip, float volume = 0.6f) const {
        if (audio != nullptr && clip != nullptr && clip->Valid())
            audio->PlaySound(*clip, volume);
    }

    int score = 0;
    int coins = 0;
    int lives = 3;

    std::function<std::unique_ptr<Scene>()> makeTitle;
    std::function<std::unique_ptr<Scene>(int)> makeGame;

    void DrawTextCentered(const std::string& text, float y, float pixelSize, const Color& color) {
        const float w = font->Measure(text, pixelSize).x;
        renderer->DrawText(*font, text, {(screenW - w) * 0.5f, y}, color, pixelSize);
    }
};
} // namespace

// ===========================================================================
// PauseScene — overlay: freezes gameplay (blocks update) but lets it show
// through (does not block render).
// ===========================================================================
class PauseScene : public Scene {
public:
    explicit PauseScene(GameContext* ctx) : m_ctx(ctx) {}

    bool BlocksRender() const override { return false; }

    void OnUpdate(float) override {
        if (Input::IsKeyPressed(Key::Enter) || Input::IsKeyPressed(Key::P))
            Manager().Pop(); // resume
        else if (Input::IsKeyPressed(Key::Escape))
            Manager().Change(m_ctx->makeTitle()); // quit to title
    }

    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;
        r.Begin(Renderer2D::ScreenCamera(m_ctx->screenW, m_ctx->screenH));
        r.DrawQuad({0.0f, 0.0f}, {m_ctx->screenW, m_ctx->screenH}, Color(0.0f, 0.0f, 0.0f, 0.6f));
        m_ctx->DrawTextCentered("PAUSED", 200.0f, 96.0f, Color(1.0f, 1.0f, 1.0f));
        m_ctx->DrawTextCentered("ENTER  RESUME", 310.0f, 36.0f, Color(0.85f, 0.95f, 0.85f));
        m_ctx->DrawTextCentered("ESC  QUIT TO TITLE", 360.0f, 36.0f, Color(0.95f, 0.85f, 0.8f));
        r.End();
    }

private:
    GameContext* m_ctx;
};

// ===========================================================================
// GameScene — the platformer, driven by the shared PhysicsWorld:
// solid tiles -> static bodies, player + goombas -> dynamic bodies.
// ===========================================================================
class GameScene : public Scene {
public:
    GameScene(GameContext* ctx, int level) : m_ctx(ctx), m_level(level) {}

    void OnEnter() override {
        const std::unordered_map<char, int> legend = {
            {'#', GROUND}, {'=', DIRT}, {'b', BRICK}, {'?', QUESTION}, {'(', PIPE_TL},
            {')', PIPE_TR}, {'{', PIPE_BL}, {'}', PIPE_BR}, {'X', HARD}, {'o', COIN}};

        const std::string path = m_ctx->assetsDir + "/level" + std::to_string(m_level) + ".txt";
        TileLevel level = LoadTileLevel(path, kT, kT, m_ctx->tileset, legend);
        m_map = std::move(level.map);
        m_worldSize = m_map.WorldSize();

        m_camera = Camera2D(m_ctx->screenW, m_ctx->screenH);
        m_camera.SetZoom(kZoom); // before SetBounds: clamping uses the zoomed view size
        m_camera.SetBounds({0.0f, 0.0f}, m_worldSize);

        m_goombas.clear();
        m_mushrooms.clear();
        m_mushroomBlocks.clear();
        m_form = MarioForm::Small;
        m_invulnTimer = 0.0f;
        m_spawn = {120.0f, kT * 14.0f};

        // First pass: any '!' marker is a mushroom block. Promote it to a
        // QUESTION tile NOW so the next step's BuildTileColliders sees it as
        // solid; remember the position so the bump handler knows to spawn a
        // mushroom there instead of a coin.
        for (const LevelObject& obj : level.objects) {
            if (obj.symbol == '!') {
                m_map.SetTile(obj.tileX, obj.tileY, QUESTION);
                m_mushroomBlocks.insert({obj.tileX, obj.tileY});
            }
        }

        // Physics world: solid tiles become static bodies (dynamic bodies for
        // actors). Adjacent solids are merged into larger rectangles (greedy
        // meshing) so the player slides along smooth surfaces instead of catching
        // on the seams between per-tile colliders.
        m_world.Clear();
        m_world.SetGravity({0.0f, kGravity});
        BuildTileColliders();

        // Second pass: spawn points and dynamic actors.
        for (const LevelObject& obj : level.objects) {
            const float px = obj.tileX * kT;
            if (obj.symbol == 'M') {
                m_spawn = {px, (obj.tileY + 1) * kT - kPlayerSmallH};
            } else if (obj.symbol == 'g') {
                Body gb;
                gb.type = BodyType::Dynamic;
                gb.tag = TagGoomba;
                gb.size = {kGoombaW, kGoombaH};
                gb.position = {px, (obj.tileY + 1) * kT - kGoombaH};
                Goomba g;
                g.bodyId = m_world.AddBody(gb);
                g.lastPos = gb.position;
                g.dir = -1.0f;
                m_goombas.push_back(g);
            }
        }

        Body player;
        player.type = BodyType::Dynamic;
        player.tag = TagPlayer;
        player.size = {kPlayerSmallW, kPlayerSmallH};
        player.position = m_spawn;
        m_playerId = m_world.AddBody(player);
        m_facing = 1.0f;
        m_walkFrame = 0;
        m_walkTimer = 0.0f;

        if (m_ctx->audio != nullptr && m_ctx->music != nullptr && m_ctx->music->Valid())
            m_ctx->audio->PlayMusic(*m_ctx->music, 0.35f, true);
    }

    void OnExit() override {
        if (m_ctx->audio != nullptr)
            m_ctx->audio->StopMusic();
    }

    void OnUpdate(float dtRaw) override {
        if (Input::IsKeyPressed(Key::P) || Input::IsKeyPressed(Key::Escape)) {
            Manager().Push(std::make_unique<PauseScene>(m_ctx));
            return;
        }

        const float dt = Min(dtRaw, 1.0f / 30.0f);
        if (m_invulnTimer > 0.0f) m_invulnTimer -= dt;

        // Coin pops + score popups always animate (they're decorative and
        // independent of gameplay state).
        UpdateCoinPops(dt);
        UpdateScorePops(dt);

        // Transform animation: freeze gameplay, hold velocities at zero.
        if (m_transformTimer > 0.0f) {
            m_transformTimer -= dt;
            if (Body* p = m_world.Get(m_playerId)) p->velocity = {0.0f, 0.0f};
            for (Goomba& g : m_goombas) {
                if (Body* gb = m_world.Get(g.bodyId)) gb->velocity = {0.0f, 0.0f};
            }
            m_particles.Update(dt);
            return;
        }

        ControlPlayer(dt);
        ControlGoombas(dt);
        ControlMushrooms(dt);

        m_world.Step(dt); // integrates gravity + resolves tile collisions

        TurnGoombasAtWalls();
        TurnMushroomsAtWalls();
        BumpBlocks();
        CollectCoins();
        CollectMushrooms();
        ResolveGoombaHits();
        m_particles.Update(dt);

        Body* p = m_world.Get(m_playerId);
        if (p == nullptr)
            return;
        if (p->position.y > m_worldSize.y + 80.0f) {
            Die();
            return;
        }
        if (p->position.x + p->size.x >= m_worldSize.x - 2.0f) {
            NextLevel();
            return;
        }
        m_camera.CenterOn({p->position.x + p->size.x * 0.5f, p->position.y + p->size.y * 0.5f});
    }

    void OnRender() override {
        Application::Get().GetDevice().Clear(Color(0.42f, 0.55f, 1.0f, 1.0f));
        Renderer2D& r = *m_ctx->renderer;

        r.Begin(m_camera.ViewProjection());
        m_map.Draw(r, m_camera.VisibleBounds());
        for (const Goomba& g : m_goombas) {
            if (!g.alive)
                continue;
            if (g.squashTimer > 0.0f) {
                r.DrawQuad({g.lastPos.x, g.lastPos.y + kGoombaH - 10.0f}, {kGoombaW, 10.0f},
                           m_ctx->chars->Get("goombaSquashed"));
            } else if (const Body* gb = m_world.Get(g.bodyId)) {
                r.DrawQuad(gb->position, {kGoombaW, kGoombaH},
                           m_ctx->chars->Get(g.frame ? "goomba1" : "goomba0"));
            }
        }
        // Mushrooms (drawn under the player so a mushroom emerging from a
        // block reads as "behind" the action briefly).
        for (const Mushroom& mu : m_mushrooms) {
            if (!mu.alive) continue;
            if (const Body* mb = m_world.Get(mu.bodyId)) {
                r.DrawQuad(mb->position, {kMushroomW, kMushroomH},
                           m_ctx->chars->Get("mushroom"));
            }
        }

        if (const Body* p = m_world.Get(m_playerId)) {
            const bool flicker = (m_invulnTimer > 0.0f) &&
                                 (static_cast<int>(m_invulnTimer * 10.0f) % 2 == 0);
            if (!flicker) {
                // While transform anim runs, alternate between the OLD and
                // NEW forms every ~0.1s -- SMB-style grow flash. After it
                // ends, render the new form normally.
                MarioForm renderForm = m_form;
                if (m_transformTimer > 0.0f &&
                    static_cast<int>(m_transformTimer * 10.0f) % 2 == 0) {
                    renderForm = m_transformFrom;
                }
                const bool airborne = !p->onGround && m_transformTimer <= 0.0f;
                const bool moving   = std::fabs(p->velocity.x) > 5.0f && m_walkFrame;
                const char* name;
                if (renderForm == MarioForm::Super)
                    name = airborne ? "superJump" : (moving ? "superWalk" : "superStand");
                else
                    name = airborne ? "jump" : (moving ? "walk" : "stand");
                // When we're flashing the old (smaller) Small form during a
                // grow animation, drop the quad size so the Small sprite
                // doesn't stretch to fill the Super hitbox; keep the feet
                // pinned to the body bottom either way.
                Vec2 spriteSize = p->size;
                Vec2 spritePos  = p->position;
                if (renderForm == MarioForm::Small && m_form == MarioForm::Super) {
                    spriteSize = {kPlayerSmallW, kPlayerSmallH};
                    spritePos.y = p->position.y + (p->size.y - kPlayerSmallH);
                } else if (renderForm == MarioForm::Super && m_form == MarioForm::Small) {
                    spriteSize = {kPlayerSuperW, kPlayerSuperH};
                    spritePos.y = p->position.y + (p->size.y - kPlayerSuperH);
                }
                r.DrawQuad(spritePos, spriteSize, m_ctx->chars->Get(name),
                           Color::White(), m_facing < 0.0f);
            }
        }

        // Coin pops: a coin sprite that rises out of a bumped block then
        // drops back. Uses the tilemap's coin tile so the look matches the
        // floating coins exactly. Drawn after the player so they pop in
        // front when Mario is touching the block.
        for (const CoinPop& cp : m_coinPops) {
            const float u = cp.t / kCoinPopLife;     // 0..1
            const float arc = 1.0f - (2.0f * u - 1.0f) * (2.0f * u - 1.0f); // 0..1..0
            const Vec2 pos{cp.origin.x - kT * 0.5f, cp.origin.y - kCoinPopRise * arc - kT};
            r.DrawQuad(pos, {kT, kT}, m_ctx->tileset.At(COIN));
        }
        m_particles.Draw(r);

        // Floating score labels rise + fade. Drawn last so they sit on top
        // of everything in the world.
        for (const ScorePop& sp : m_scorePops) {
            const float u = sp.t / kScorePopLife;
            const float alpha = 1.0f - u;
            const Vec2 pos{sp.origin.x, sp.origin.y - kScorePopRise * u};
            char buf[16];
            std::snprintf(buf, sizeof(buf), "+%d", sp.value);
            // 14 px text size fits the world's 32 px tile grid (zoom 2x).
            const Vec2 sz = m_ctx->font->Measure(buf, 14.0f);
            r.DrawText(*m_ctx->font, buf, {pos.x - sz.x * 0.5f, pos.y},
                       Color(1.0f, 1.0f, 1.0f, alpha), 14.0f);
        }

        r.End();

        r.Begin(Renderer2D::ScreenCamera(m_ctx->screenW, m_ctx->screenH));
        r.DrawText(*m_ctx->font, "SCORE " + std::to_string(m_ctx->score), {16.0f, 14.0f},
                   Color(1.0f, 1.0f, 1.0f), 22.0f);
        r.DrawText(*m_ctx->font, "COINS " + std::to_string(m_ctx->coins), {280.0f, 14.0f},
                   Color(1.0f, 0.9f, 0.4f), 22.0f);
        r.DrawText(*m_ctx->font, "WORLD " + std::to_string(m_level), {520.0f, 14.0f},
                   Color(0.8f, 0.9f, 1.0f), 22.0f);
        r.DrawText(*m_ctx->font, "LIVES " + std::to_string(m_ctx->lives),
                   {m_ctx->screenW - 130.0f, 14.0f}, Color(0.8f, 1.0f, 0.8f), 22.0f);
        r.End();
    }

private:
    // Merge contiguous solid tiles into maximal rectangles and add one static
    // body per rectangle. Removes the internal seams that a fast/jumping body
    // can otherwise snag on.
    void BuildTileColliders() {
        const int mw = m_map.Width();
        const int mh = m_map.Height();
        std::vector<char> used(static_cast<size_t>(mw) * mh, 0);
        auto solid = [&](int x, int y) { return IsSolidTile(m_map.GetTile(x, y)); };

        for (int y = 0; y < mh; ++y) {
            for (int x = 0; x < mw; ++x) {
                if (used[y * mw + x] || !solid(x, y))
                    continue;
                int w = 1;
                while (x + w < mw && solid(x + w, y) && !used[y * mw + x + w])
                    ++w;
                int h = 1;
                bool extend = true;
                while (y + h < mh && extend) {
                    for (int xx = x; xx < x + w; ++xx) {
                        if (!solid(xx, y + h) || used[(y + h) * mw + xx]) {
                            extend = false;
                            break;
                        }
                    }
                    if (extend)
                        ++h;
                }
                for (int yy = y; yy < y + h; ++yy)
                    for (int xx = x; xx < x + w; ++xx)
                        used[yy * mw + xx] = 1;

                Body box;
                box.type = BodyType::Static;
                box.tag = TagTile;
                box.position = {x * kT, y * kT};
                box.size = {w * kT, h * kT};
                m_world.AddBody(box);
            }
        }
    }

    // Drive the player from input. Velocity changes are gradual (accel +
    // friction + skid) so Mario carries momentum the way SMB1 does, instead
    // of snapping to a target speed every frame. Coyote time + jump buffer
    // make ledge-jumps and land-jumps forgiving.
    void ControlPlayer(float dt) {
        Body* p = m_world.Get(m_playerId);
        if (p == nullptr)
            return;

        float dir = 0.0f;
        if (Input::IsKeyDown(Key::Left) || Input::IsKeyDown(Key::A))
            dir -= 1.0f;
        if (Input::IsKeyDown(Key::Right) || Input::IsKeyDown(Key::D))
            dir += 1.0f;
        // Shift = run, like the B button in SMB.
        const bool running = Input::IsKeyDown(Key::LeftShift);
        const bool airborne = !p->onGround;
        const float maxSpeed = running ? kRunSpeed : kWalkSpeed;

        const float vx = p->velocity.x;
        float newVx = vx;
        const float accel = airborne ? kAirAccel : kGroundAccel;
        const float decel = airborne ? kAirDecel : kGroundDecel;
        if (dir != 0.0f) {
            const float wantSign = dir;
            const float curSign  = (vx > 0.0f) ? 1.0f : (vx < 0.0f ? -1.0f : 0.0f);
            const bool reversing = curSign != 0.0f && curSign != wantSign;
            // Skid: while still moving in the wrong direction, decel hard --
            // this is the "feet sliding" pivot at the start of every direction
            // change. Once vx crosses zero, normal accel takes over.
            const float a = reversing ? kSkidDecel : accel;
            newVx = vx + dir * a * dt;
            // Clamp at max speed in the input direction.
            if (dir > 0.0f && newVx > maxSpeed) newVx = maxSpeed;
            if (dir < 0.0f && newVx < -maxSpeed) newVx = -maxSpeed;
        } else if (vx > 0.0f) {
            newVx = Max(0.0f, vx - decel * dt);
        } else if (vx < 0.0f) {
            newVx = Min(0.0f, vx + decel * dt);
        }
        // Walking demotes a sprint-state velocity gradually -- you don't
        // instantly cap when releasing Shift, you coast down through accel.
        if (dir != 0.0f && !running) {
            if (newVx >  kWalkSpeed) newVx = Max( kWalkSpeed, newVx - decel * dt);
            if (newVx < -kWalkSpeed) newVx = Min(-kWalkSpeed, newVx + decel * dt);
        }
        p->velocity.x = newVx;

        if (dir != 0.0f) m_facing = dir;

        // Coyote time: refill while grounded, drain in the air.
        if (p->onGround) m_coyoteTimer = kCoyoteSeconds;
        else m_coyoteTimer = Max(0.0f, m_coyoteTimer - dt);

        // Jump buffer: any fresh press refills the buffer; it drains in time
        // so an over-eager press still fires if Mario lands within the window.
        const bool jumpPressed = Input::IsKeyPressed(Key::Space) ||
                                 Input::IsKeyPressed(Key::Up) || Input::IsKeyPressed(Key::W);
        const bool jumpHeld = Input::IsKeyDown(Key::Space) || Input::IsKeyDown(Key::Up) ||
                              Input::IsKeyDown(Key::W);
        if (jumpPressed) m_jumpBuffer = kJumpBufferSeconds;
        else m_jumpBuffer = Max(0.0f, m_jumpBuffer - dt);

        if (m_jumpBuffer > 0.0f && m_coyoteTimer > 0.0f) {
            const float speedFrac = Min(std::fabs(p->velocity.x) / kRunSpeed, 1.0f);
            p->velocity.y = -(kJumpSpeed + kRunJumpBonus * speedFrac);
            m_ctx->Sfx(m_ctx->sfxJump);
            m_jumpBuffer = 0.0f;
            m_coyoteTimer = 0.0f;
        }

        // Variable-height jump: heavier gravity while rising without holding jump.
        p->gravityScale = (p->velocity.y < 0.0f && !jumpHeld) ? kFastFall : 1.0f;
        if (p->velocity.y > kMaxFall)
            p->velocity.y = kMaxFall;

        if (std::fabs(p->velocity.x) > 5.0f && p->onGround) {
            m_walkTimer += dt;
            if (m_walkTimer >= 0.12f) { m_walkTimer = 0.0f; m_walkFrame ^= 1; }
        } else {
            m_walkFrame = 0;
        }
    }

    void ControlGoombas(float dt) {
        for (Goomba& g : m_goombas) {
            if (!g.alive)
                continue;
            if (g.squashTimer > 0.0f) {
                g.squashTimer -= dt;
                if (g.squashTimer <= 0.0f)
                    g.alive = false;
                continue;
            }
            Body* gb = m_world.Get(g.bodyId);
            if (gb == nullptr || !gb->active)
                continue;
            gb->velocity.x = g.dir * kGoombaSpeed;
            if (gb->velocity.y > kMaxFall)
                gb->velocity.y = kMaxFall;
            g.lastPos = gb->position;
            g.animTimer += dt;
            if (g.animTimer >= 0.2f) { g.animTimer = 0.0f; g.frame ^= 1; }
        }
    }

    void ControlMushrooms(float dt) {
        for (Mushroom& mu : m_mushrooms) {
            if (!mu.alive) continue;
            Body* mb = m_world.Get(mu.bodyId);
            if (mb == nullptr) continue;
            if (mu.spawnTimer > 0.0f) {
                // Rise up out of the block: float upward briefly with no
                // horizontal motion, ignoring gravity, then start rolling.
                mu.spawnTimer -= dt;
                mb->gravityScale = 0.0f;
                mb->velocity = {0.0f, -45.0f};
                if (mu.spawnTimer <= 0.0f) {
                    mb->gravityScale = 1.0f;
                    mb->velocity.y = 0.0f;
                }
                continue;
            }
            mb->velocity.x = mu.dir * kMushroomSpeed;
            if (mb->velocity.y > kMaxFall) mb->velocity.y = kMaxFall;
        }
    }

    void TurnMushroomsAtWalls() {
        for (const Contact& c : m_world.Contacts()) {
            if (std::fabs(c.normal.x) < 0.5f) continue;
            for (Mushroom& mu : m_mushrooms) {
                if (mu.alive && mu.bodyId == c.a) {
                    mu.dir = (c.normal.x > 0.0f) ? 1.0f : -1.0f;
                    break;
                }
            }
        }
        for (Mushroom& mu : m_mushrooms) {
            if (!mu.alive) continue;
            Body* mb = m_world.Get(mu.bodyId);
            if (mb != nullptr && mb->position.y > m_worldSize.y + 80.0f) {
                mu.alive = false;
                m_world.RemoveBody(mu.bodyId);
            }
        }
    }

    void CollectMushrooms() {
        Body* p = m_world.Get(m_playerId);
        if (p == nullptr) return;
        for (Mushroom& mu : m_mushrooms) {
            if (!mu.alive) continue;
            Body* mb = m_world.Get(mu.bodyId);
            if (mb == nullptr) continue;
            const bool overlap = p->position.x < mb->position.x + kMushroomW &&
                                 p->position.x + p->size.x > mb->position.x &&
                                 p->position.y < mb->position.y + kMushroomH &&
                                 p->position.y + p->size.y > mb->position.y;
            if (!overlap) continue;
            mu.alive = false;
            m_world.RemoveBody(mu.bodyId);
            m_ctx->score += 1000;
            m_ctx->Sfx(m_ctx->sfxCoin);
            SpawnScorePop({mb->position.x + kMushroomW * 0.5f,
                           mb->position.y - 4.0f}, 1000);
            GrowToSuper();
        }
    }

    void SpawnMushroom(int tx, int ty) {
        // Comes out of the top of the bumped block. The mushroom briefly
        // floats up before physics takes over, like the SMB1 reveal.
        Body mb;
        mb.type = BodyType::Dynamic;
        mb.tag = TagMushroom;
        mb.size = {kMushroomW, kMushroomH};
        mb.position = {tx * kT + (kT - kMushroomW) * 0.5f,
                       ty * kT - kMushroomH};
        mb.gravityScale = 0.0f; // suspended until spawn animation ends
        Mushroom mu;
        mu.bodyId = m_world.AddBody(mb);
        mu.dir = 1.0f;
        mu.spawnTimer = 0.45f;
        mu.spawnAt = mb.position;
        m_mushrooms.push_back(mu);
    }

    // Resize the player's AABB to Super dimensions. Body bottom stays put so
    // the top (and rendered sprite) extends upward by an extra tile. The
    // transform timer kicks off the SMB-style flash + freeze.
    void GrowToSuper() {
        if (m_form == MarioForm::Super) return;
        Body* p = m_world.Get(m_playerId);
        if (p == nullptr) return;
        const float feetY = p->position.y + p->size.y;
        p->size = {kPlayerSuperW, kPlayerSuperH};
        p->position.y = feetY - kPlayerSuperH;
        m_transformFrom = MarioForm::Small;
        m_form = MarioForm::Super;
        m_transformTimer = kTransformSeconds;
    }

    void ShrinkToSmall() {
        if (m_form == MarioForm::Small) return;
        Body* p = m_world.Get(m_playerId);
        if (p == nullptr) return;
        const float feetY = p->position.y + p->size.y;
        p->size = {kPlayerSmallW, kPlayerSmallH};
        p->position.y = feetY - kPlayerSmallH;
        m_transformFrom = MarioForm::Super;
        m_form = MarioForm::Small;
        m_transformTimer = kTransformSeconds;
        m_invulnTimer = kInvulnSeconds;
    }

    // --- Polish: coin pops + score popups ---------------------------------

    void SpawnCoinPop(int tx, int ty) {
        CoinPop cp;
        cp.origin = {tx * kT + kT * 0.5f, ty * kT};
        m_coinPops.push_back(cp);
    }

    void UpdateCoinPops(float dt) {
        for (CoinPop& cp : m_coinPops) cp.t += dt;
        m_coinPops.erase(
            std::remove_if(m_coinPops.begin(), m_coinPops.end(),
                           [](const CoinPop& cp) { return cp.t >= kCoinPopLife; }),
            m_coinPops.end());
    }

    void SpawnScorePop(Vec2 worldPos, int value) {
        ScorePop sp;
        sp.origin = worldPos;
        sp.value = value;
        m_scorePops.push_back(sp);
    }

    void UpdateScorePops(float dt) {
        for (ScorePop& sp : m_scorePops) sp.t += dt;
        m_scorePops.erase(
            std::remove_if(m_scorePops.begin(), m_scorePops.end(),
                           [](const ScorePop& sp) { return sp.t >= kScorePopLife; }),
            m_scorePops.end());
    }

    // A goomba that hit a wall (horizontal contact normal) reverses direction.
    void TurnGoombasAtWalls() {
        for (const Contact& c : m_world.Contacts()) {
            if (std::fabs(c.normal.x) < 0.5f)
                continue;
            for (Goomba& g : m_goombas) {
                if (g.alive && g.squashTimer <= 0.0f && g.bodyId == c.a) {
                    g.dir = (c.normal.x > 0.0f) ? 1.0f : -1.0f;
                    break;
                }
            }
        }
        // Goombas that fell into a pit.
        for (Goomba& g : m_goombas) {
            if (!g.alive || g.squashTimer > 0.0f)
                continue;
            Body* gb = m_world.Get(g.bodyId);
            if (gb != nullptr && gb->position.y > m_worldSize.y + 80.0f) {
                g.alive = false;
                m_world.RemoveBody(g.bodyId);
            }
        }
    }

    void ResolveGoombaHits() {
        Body* p = m_world.Get(m_playerId);
        if (p == nullptr)
            return;
        for (Goomba& g : m_goombas) {
            if (!g.alive || g.squashTimer > 0.0f)
                continue;
            Body* gb = m_world.Get(g.bodyId);
            if (gb == nullptr || !gb->active)
                continue;
            const bool overlap = p->position.x < gb->position.x + kGoombaW &&
                                 p->position.x + p->size.x > gb->position.x &&
                                 p->position.y < gb->position.y + kGoombaH &&
                                 p->position.y + p->size.y > gb->position.y;
            if (!overlap)
                continue;
            const bool stomp = p->velocity.y > 0.0f &&
                               (p->position.y + p->size.y) < gb->position.y + kGoombaH * 0.6f;
            if (stomp) {
                g.lastPos = gb->position;
                g.squashTimer = 0.35f;
                m_world.RemoveBody(g.bodyId);
                p->velocity.y = -kStompBounce;
                m_ctx->score += 200;
                m_ctx->Sfx(m_ctx->sfxStomp);
                SpawnScorePop({gb->position.x + kGoombaW * 0.5f,
                               gb->position.y - 4.0f}, 200);
                ParticleBurst burst;
                burst.position = {gb->position.x + kGoombaW * 0.5f, gb->position.y + kGoombaH};
                burst.count = 14;
                burst.minSpeed = 40.0f;
                burst.maxSpeed = 170.0f;
                burst.startSize = 6.0f;
                burst.startColor = Color(0.6f, 0.4f, 0.2f);
                burst.endColor = Color(0.6f, 0.4f, 0.2f, 0.0f);
                burst.gravity = {0.0f, 600.0f};
                m_particles.Emit(burst);
            } else if (m_invulnTimer > 0.0f) {
                continue; // mercy frames after a recent hit
            } else if (m_form == MarioForm::Super) {
                ShrinkToSmall();
                m_ctx->Sfx(m_ctx->sfxStomp);
                return;
            } else {
                Die();
                return;
            }
        }
    }

    // Bumping a ? block from below pops a coin and turns it into a used (still
    // solid) block. The head-bump is detected from this frame's contacts; the
    // struck tile is the one directly above the player's head.
    void BumpBlocks() {
        const Body* p = m_world.Get(m_playerId);
        if (p == nullptr)
            return;

        bool headHit = false;
        for (const Contact& c : m_world.Contacts()) {
            if (c.a == m_playerId && c.normal.y > 0.5f) { headHit = true; break; }
        }
        if (!headHit)
            return;

        const int row = static_cast<int>(std::floor((p->position.y - 2.0f) / kT));
        const int left = static_cast<int>(std::floor(p->position.x / kT));
        const int right = static_cast<int>(std::floor((p->position.x + p->size.x - 1.0f) / kT));
        for (int tx = left; tx <= right; ++tx) {
            if (m_map.GetTile(tx, row) != QUESTION)
                continue;
            m_map.SetTile(tx, row, USED);
            const bool isMushroomBlock = m_mushroomBlocks.count({tx, row}) > 0;
            m_mushroomBlocks.erase({tx, row});
            if (isMushroomBlock) {
                SpawnMushroom(tx, row);
                m_ctx->Sfx(m_ctx->sfxCoin);
            } else {
                ++m_ctx->coins;
                m_ctx->score += 200;
                m_ctx->Sfx(m_ctx->sfxCoin);
                SpawnCoinPop(tx, row);
                SpawnScorePop({tx * kT + kT * 0.5f, row * kT - 6.0f}, 200);
            }
            break; // one block per bump
        }
    }

    void CollectCoins() {
        const Body* p = m_world.Get(m_playerId);
        if (p == nullptr)
            return;
        const int left = static_cast<int>(std::floor(p->position.x / kT));
        const int right = static_cast<int>(std::floor((p->position.x + p->size.x - 1.0f) / kT));
        const int top = static_cast<int>(std::floor(p->position.y / kT));
        const int bottom = static_cast<int>(std::floor((p->position.y + p->size.y - 1.0f) / kT));
        for (int ty = top; ty <= bottom; ++ty)
            for (int tx = left; tx <= right; ++tx)
                if (m_map.GetTile(tx, ty) == COIN) {
                    m_map.SetTile(tx, ty, EMPTY);
                    ++m_ctx->coins;
                    m_ctx->score += 100;
                    m_ctx->Sfx(m_ctx->sfxCoin);
                    SpawnScorePop({tx * kT + kT * 0.5f, ty * kT + kT * 0.5f}, 100);
                    ParticleBurst burst;
                    burst.position = {tx * kT + kT * 0.5f, ty * kT + kT * 0.5f};
                    burst.count = 12;
                    burst.minSpeed = 50.0f;
                    burst.maxSpeed = 150.0f;
                    burst.minLife = 0.25f;
                    burst.maxLife = 0.5f;
                    burst.startSize = 5.0f;
                    burst.startColor = Color(1.0f, 0.9f, 0.3f);
                    burst.endColor = Color(1.0f, 0.9f, 0.3f, 0.0f);
                    m_particles.Emit(burst);
                }
    }

    void RespawnPlayer() {
        if (Body* p = m_world.Get(m_playerId)) {
            p->position = m_spawn;
            p->size = {kPlayerSmallW, kPlayerSmallH};
            p->velocity = {0.0f, 0.0f};
            p->onGround = false;
        }
        m_form = MarioForm::Small;
        m_invulnTimer = kInvulnSeconds; // brief grace after respawn too
        m_facing = 1.0f;
        m_camera.CenterOn(m_spawn);
    }

    void Die() {
        if (--m_ctx->lives <= 0)
            Manager().Change(m_ctx->makeTitle());
        else
            RespawnPlayer();
    }

    void NextLevel() {
        if (m_level < kMaxLevel)
            Manager().Change(m_ctx->makeGame(m_level + 1));
        else
            Manager().Change(m_ctx->makeTitle()); // game complete
    }

    GameContext* m_ctx;
    int m_level;
    Tilemap m_map;
    Camera2D m_camera;
    Vec2 m_worldSize;
    Vec2 m_spawn;

    PhysicsWorld m_world;
    ParticleSystem m_particles;
    int m_playerId = -1;
    std::vector<Goomba> m_goombas;
    std::vector<Mushroom> m_mushrooms;
    std::vector<CoinPop> m_coinPops;
    std::vector<ScorePop> m_scorePops;
    std::set<std::pair<int,int>> m_mushroomBlocks;
    MarioForm m_form = MarioForm::Small;
    float m_invulnTimer = 0.0f;
    float m_transformTimer = 0.0f;    // > 0 while grow/shrink animation runs
    MarioForm m_transformFrom = MarioForm::Small; // form we're animating away from
    float m_coyoteTimer = 0.0f;       // jump-from-ledge grace window
    float m_jumpBuffer = 0.0f;        // pre-landing jump press buffer
    bool m_wasRunning = false;        // was Shift held last frame (for run-jump impulse)
    float m_facing = 1.0f;
    float m_walkTimer = 0.0f;
    int m_walkFrame = 0;
};

// ===========================================================================
// TitleScene
// ===========================================================================
class TitleScene : public Scene {
public:
    explicit TitleScene(GameContext* ctx) : m_ctx(ctx) {}

    void OnUpdate(float dt) override {
        m_time += dt;
        if (Input::IsKeyPressed(Key::Enter)) {
            m_ctx->score = 0; // fresh run
            m_ctx->coins = 0;
            m_ctx->lives = 3;
            Manager().Change(m_ctx->makeGame(1));
        } else if (Input::IsKeyPressed(Key::Escape)) {
            Application::Get().Close();
        }
    }

    void OnRender() override {
        Application::Get().GetDevice().Clear(Color(0.10f, 0.12f, 0.28f, 1.0f));
        Renderer2D& r = *m_ctx->renderer;
        r.Begin(Renderer2D::ScreenCamera(m_ctx->screenW, m_ctx->screenH));
        m_ctx->DrawTextCentered("N A R A", 130.0f, 120.0f, Color(0.95f, 0.3f, 0.3f));
        m_ctx->DrawTextCentered("SUPER MARIO", 250.0f, 72.0f, Color(1.0f, 1.0f, 1.0f));
        // Blink the prompt.
        if (std::fmod(m_time, 1.0f) < 0.65f)
            m_ctx->DrawTextCentered("PRESS ENTER TO START", 360.0f, 36.0f,
                                    Color(0.9f, 0.9f, 0.6f));
        m_ctx->DrawTextCentered("A/D MOVE   SPACE JUMP   P PAUSE", 470.0f, 24.0f,
                                Color(0.6f, 0.65f, 0.8f));
        // Tagline now uses the same MSDF font as the rest -- Inter handles
        // proportional widths natively, no separate BMFont needed.
        {
            const std::string tag = "Built from scratch with the NaraGame engine";
            const float px = 18.0f;
            const float w = m_ctx->font->Measure(tag, px).x;
            r.DrawText(*m_ctx->font, tag, {(m_ctx->screenW - w) * 0.5f, 530.0f},
                       Color(0.7f, 0.85f, 1.0f), px);
        }
        r.End();
    }

private:
    GameContext* m_ctx;
    float m_time = 0.0f;
};

// ===========================================================================
// Application
// ===========================================================================
class MarioApp : public Application {
public:
    MarioApp() : Application(MakeConfig()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;

        // Point sampling on both atlases so the SMB-style pixel art stays
        // crisp at any zoom -- linear would smear the 2x source pixels.
        m_tilesTexture = LoadTexture(GetDevice(), assets + "/tiles.png", TextureFilter::Point);
        m_charTexture  = LoadTexture(GetDevice(), assets + "/characters.png", TextureFilter::Point);
        m_font = LoadMSDFFont(GetDevice(), assets + "/msdf-inter.json");

        m_jump = LoadWav(assets + "/jump.wav");
        m_coin = LoadWav(assets + "/coin.wav");
        m_stomp = LoadWav(assets + "/stomp.wav");
        m_bgm = LoadWav(assets + "/bgm.wav");

        // New atlas layout: 3x32 mario frames on row 0, 3x32 goomba frames
        // on row 1. Each cell is one tile (kT x kT).
        // Atlas layout (128 x 128):
        //   row 0 (y 0..31):  small stand/walk/jump (3 cells of 32x32), then mushroom
        //   row 1 (y 32..63): goomba0/goomba1/squashed (3 cells of 32x32)
        //   rows 2-3 (y 64..127, each cell 32x64): super stand/walk/jump
        m_chars = TextureAtlas(m_charTexture.get());
        m_chars.Add("stand",          0.0f,  0.0f, kT, kT);
        m_chars.Add("walk",           kT,    0.0f, kT, kT);
        m_chars.Add("jump",           kT*2,  0.0f, kT, kT);
        m_chars.Add("mushroom",       kT*3,  0.0f, kT, kT);
        m_chars.Add("goomba0",        0.0f,  kT,   kT, kT);
        m_chars.Add("goomba1",        kT,    kT,   kT, kT);
        m_chars.Add("goombaSquashed", kT*2,  kT,   kT, kT);
        m_chars.Add("superStand",     0.0f,  kT*2, kT, kT*2);
        m_chars.Add("superWalk",      kT,    kT*2, kT, kT*2);
        m_chars.Add("superJump",      kT*2,  kT*2, kT, kT*2);

        m_ctx.renderer = &m_renderer;
        m_ctx.font = &m_font;
        m_ctx.chars = &m_chars;
        m_ctx.tilesTexture = m_tilesTexture.get();
        m_ctx.tileset = SpriteSheet(m_tilesTexture.get(), kTile, kTile);
        m_ctx.assetsDir = assets;
        m_ctx.audio = &GetAudio();
        m_ctx.sfxJump = &m_jump;
        m_ctx.sfxCoin = &m_coin;
        m_ctx.sfxStomp = &m_stomp;
        m_ctx.music = &m_bgm;
        m_ctx.makeTitle = [this]() { return std::make_unique<TitleScene>(&m_ctx); };
        m_ctx.makeGame = [this](int level) { return std::make_unique<GameScene>(&m_ctx, level); };

        m_scenes.Change(m_ctx.makeTitle());
        NARAGAME_INFO("Mario: scene/state demo (title -> game <-> pause, file-loaded levels)");
    }

    void OnStop() override {
        m_tilesTexture.reset();
        m_charTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override { m_scenes.Update(dt); }
    void OnRender() override { m_scenes.Render(); }

private:
    static AppConfig MakeConfig() {
        AppConfig config;
        config.title = "NaraGame - Super Mario";
        config.width = static_cast<uint32_t>(kScreenW);
        config.height = static_cast<uint32_t>(kScreenH);
        return config;
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_tilesTexture;
    std::unique_ptr<RHITexture> m_charTexture;
    MSDFFont m_font;
    TextureAtlas m_chars;
    AudioClip m_jump;
    AudioClip m_coin;
    AudioClip m_stomp;
    AudioClip m_bgm;
    GameContext m_ctx;
    SceneManager m_scenes;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new MarioApp();
}
