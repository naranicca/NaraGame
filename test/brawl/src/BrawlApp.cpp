// Brawl Stars-style top-down twin-stick shooter (placeholder art).
//
// Look: axis-aligned grid projected with Y compressed by 0.75x — a flat
// top-down map "tilted" toward the camera. Walls extrude upward on screen by
// kWallScreenH pixels so they read as cover, not as floor decals.
// Depth uses world-Y painter's order: floor flat, then walls and entities
// sorted by feet world-Y ascending. Bullets fly over walls (no occlusion).
//
// Controls:
//   Title:     Enter start, Esc quit
//   Arena:     WASD move, mouse aim, Left-click shoot (hold to auto-fire),
//              R rebuild arena, Esc back to title
//   Game over: Enter restart, Esc title

#include <NaraGame/assets/MSDFFontLoader.h>
#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/renderer/MSDFFont.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>
#include <NaraGame/scene/Scene.h>
#include <NaraGame/scene/SceneManager.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

// ---------------------------------------------------------------------------
// Screen + world constants.
// ---------------------------------------------------------------------------
constexpr float kScreenW = 1280.0f;
constexpr float kScreenH = 720.0f;

// Tile size on screen. Y axis is compressed (75%) so a normal axis-aligned
// top-down grid reads as if the camera were tilted slightly forward — the
// brawl-stars trick that fakes a 3D look without a real 3D pass.
constexpr float kTileW = 48.0f;
constexpr float kTileH = 36.0f;
constexpr float kYCompress = kTileH / kTileW;   // 0.75 — for world->screen dirs

// Walls extrude upward on screen by this many pixels.
constexpr float kWallScreenH = 36.0f;

constexpr int   kArenaW = 26;
constexpr int   kArenaH = 18;

// Player.
constexpr float kPlayerR     = 0.36f;
constexpr float kPlayerSpeed = 5.2f;
constexpr int   kPlayerHpMax = 6;
constexpr float kPlayerHurtIFrame = 0.55f;
constexpr float kPlayerFireCd     = 0.18f;

// Bullets.
constexpr float kBulletR        = 0.13f;
constexpr float kPlayerBulletV  = 14.5f;
constexpr float kEnemyBulletV   = 8.0f;
constexpr float kBulletLife     = 0.85f;
constexpr int   kPlayerBulletDmg = 1;
constexpr int   kEnemyBulletDmg  = 1;

// Enemies.
constexpr float kEnemyR             = 0.34f;
constexpr float kEnemySpeed         = 2.6f;
constexpr int   kEnemyHpMax         = 3;
constexpr float kEnemyFireCd        = 1.3f;
constexpr float kEnemyShootRange    = 7.5f;
constexpr float kEnemyRetreatRange  = 2.5f;
constexpr float kEnemyHurtFlash     = 0.10f;
constexpr float kEnemyWanderCd      = 0.9f;

// Knockback: a bullet hit pushes the victim back along the bullet's flight
// direction. We apply it as an impulse (instantaneous velocity) that decays
// exponentially each frame and pushes through axis-separated wall collision,
// so a wall behind the victim stops the slide instead of letting them clip.
// Enemies get a bigger nudge than the player so combat reads "I hit them" but
// the player doesn't feel yanked around by every stray shot.
constexpr float kEnemyKnockbackV  = 7.5f;   // tiles/s, initial
constexpr float kPlayerKnockbackV = 4.5f;
constexpr float kKnockbackDecay   = 18.0f;  // 1/s; smaller = longer slide

// Death sparkle — four 8-pointed star particles erupting radially from a
// killed enemy. Lifetime is short and there's slight aerodynamic drag so the
// burst reads as "pop" not "drift".
constexpr float kStarSpeed = 4.5f;          // tiles/s, initial radial speed
constexpr float kStarLife  = 0.55f;         // seconds
constexpr float kStarDrag  = 3.0f;          // 1/s velocity decay
constexpr float kStarSpin  = 8.0f;          // rad/s — alternating sign per pair
constexpr float kStarPx    = 30.0f;         // screen pixels at spawn

// Kill popup — "KILL N" floats up from the death spot and fades out.
constexpr float kPopupLife = 1.20f;         // seconds
constexpr float kPopupRise = 1.40f;         // world tiles/s of upward drift
constexpr float kPopupPx   = 26.0f;         // text size in screen pixels

// Crit: independent per-shot roll on a player bullet vs enemy hit. At
// kCritMult × the base damage a crit one-shots a full-HP enemy, which is
// the rush moment — the popup grows + turns red-orange so the player
// always sees it land.
constexpr float kCritChance = 0.08f;
constexpr int   kCritMult   = 2;

// Coin drops — 2-4 coins shoot out of each killed enemy, arc upward under
// gravity, bounce on the floor (z is height in world tiles), and settle.
// Player walks through them to auto-pick up for bonus score.
constexpr int   kCoinMinPerKill = 2;
constexpr int   kCoinMaxPerKill = 4;
constexpr float kCoinGravity    = 26.0f;   // tiles/s² downward in z
constexpr float kCoinBounce     = 0.5f;    // |new vz| / |old vz| on impact
constexpr float kCoinFriction   = 0.65f;   // horizontal damp per bounce
constexpr float kCoinSettleVz   = 0.8f;    // |vz| below this on impact → stop
constexpr float kCoinInitVzMin  = 5.0f;    // initial up speed
constexpr float kCoinInitVzMax  = 7.0f;
constexpr float kCoinInitVxyMin = 0.7f;    // initial radial speed
constexpr float kCoinInitVxyMax = 1.8f;
constexpr float kCoinPickupR    = 0.55f;   // world units; player overlaps coin
constexpr float kCoinR          = 0.08f;   // collision radius vs walls
constexpr float kCoinWallBounce = 0.45f;   // per-axis bounce damping on wall
constexpr int   kCoinScore      = 5;
constexpr float kCoinSpinSpeed  = 14.0f;   // rad/s about the visible "axis"
// Sprite layout: 10 frames of the rotation cycle laid out in 5×2 cells of
// the sheet, but the cells aren't on a clean even pitch — the artist left
// padding around each frame. Hard-coded top-left coords from the source
// PNG; each frame is exactly 67×67 px and there's an 8 px gap between
// columns and 42 px between rows. We draw the quad square because the
// frame itself is square.
constexpr int   kCoinFrameCount = 10;
constexpr float kCoinFrameW     = 67.0f;
constexpr float kCoinFrameH     = 67.0f;
constexpr float kCoinSpriteW    = 22.0f;
constexpr float kCoinSpriteH    = 22.0f;

// Waves.
constexpr int   kFirstWaveSize = 3;
constexpr int   kWaveIncrement = 2;
constexpr int   kMaxWaveSize   = 12;
constexpr float kBetweenWaves  = 1.6f;
constexpr int   kScorePerKill  = 10;

// ---------------------------------------------------------------------------
// Color palette.
// ---------------------------------------------------------------------------
const Color kBgClear     = Color(0.06f, 0.08f, 0.10f, 1.0f);
const Color kFloorA      = Color(0.27f, 0.49f, 0.30f, 1.0f);
const Color kFloorB      = Color(0.23f, 0.43f, 0.27f, 1.0f);
const Color kArenaBorder = Color(0.13f, 0.17f, 0.13f, 1.0f);
const Color kWallTop     = Color(0.66f, 0.50f, 0.34f, 1.0f);
const Color kWallSide    = Color(0.36f, 0.25f, 0.16f, 1.0f);
const Color kWallEdge    = Color(0.82f, 0.66f, 0.46f, 1.0f);
const Color kPlayerBody  = Color(0.22f, 0.62f, 0.95f, 1.0f);
const Color kPlayerHead  = Color(0.92f, 0.78f, 0.58f, 1.0f);
const Color kPlayerHurt  = Color(1.00f, 0.40f, 0.40f, 1.0f);
const Color kPlayerGun   = Color(0.95f, 0.85f, 0.20f, 1.0f);
const Color kPBullet     = Color(1.00f, 0.92f, 0.30f, 1.0f);
const Color kEnemyBody   = Color(0.92f, 0.27f, 0.27f, 1.0f);
const Color kEnemyHead   = Color(0.30f, 0.10f, 0.10f, 1.0f);
const Color kEnemyGun    = Color(0.10f, 0.10f, 0.10f, 1.0f);
const Color kEBullet     = Color(0.98f, 0.42f, 0.92f, 1.0f);
const Color kShadow      = Color(0.0f, 0.0f, 0.0f, 0.30f);
const Color kHudBg       = Color(0.0f, 0.0f, 0.0f, 0.55f);
const Color kHpBarBg     = Color(0.12f, 0.06f, 0.06f, 0.85f);
const Color kHpBarFill   = Color(0.95f, 0.30f, 0.30f, 1.0f);

// ---------------------------------------------------------------------------
// World <-> screen.
// ---------------------------------------------------------------------------
inline Vec2 W2S(Vec2 w, Vec2 origin) {
    return { origin.x + w.x * kTileW, origin.y + w.y * kTileH };
}
inline Vec2 S2W(Vec2 s, Vec2 origin) {
    return { (s.x - origin.x) / kTileW, (s.y - origin.y) / kTileH };
}
// A unit world direction (dx, dy) projects to screen as (dx*tileW, dy*tileH).
// Use this when drawing rotated screen-space artefacts (aim line, gun barrel)
// so their on-screen angle matches the bullet's actual trajectory.
inline float WorldDirToScreenAngle(Vec2 worldDir) {
    return std::atan2(worldDir.y * kYCompress, worldDir.x);
}

// ---------------------------------------------------------------------------
// Tile grid + random arena.
// ---------------------------------------------------------------------------
enum class Tile : uint8_t { Floor = 0, Wall };

struct Grid {
    int w = 0, h = 0;
    std::vector<Tile> tiles;
    void Resize(int gw, int gh, Tile fill = Tile::Floor) {
        w = gw; h = gh;
        tiles.assign(size_t(gw) * gh, fill);
    }
    Tile At(int x, int y) const {
        if (x < 0 || y < 0 || x >= w || y >= h) return Tile::Wall;
        return tiles[size_t(y) * w + x];
    }
    void Set(int x, int y, Tile t) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        tiles[size_t(y) * w + x] = t;
    }
};

// Plant a handful of small wall clusters (2x2, bars, L, T) into the open arena.
// Spawn area is forcibly cleared at the end so the player never spawns inside
// a cluster the rejection sampler placed without seeing the spawn.
void BuildArena(Grid& g, uint32_t seed, Vec2& spawn) {
    g.Resize(kArenaW, kArenaH, Tile::Floor);
    for (int x = 0; x < kArenaW; ++x) {
        g.Set(x, 0, Tile::Wall);
        g.Set(x, kArenaH - 1, Tile::Wall);
    }
    for (int y = 0; y < kArenaH; ++y) {
        g.Set(0, y, Tile::Wall);
        g.Set(kArenaW - 1, y, Tile::Wall);
    }

    static const int kShapes[6][5][2] = {
        // 2x2 block
        {{0,0},{1,0},{0,1},{1,1},{-99,-99}},
        // horizontal bar 3
        {{0,0},{1,0},{2,0},{-99,-99},{-99,-99}},
        // vertical bar 3
        {{0,0},{0,1},{0,2},{-99,-99},{-99,-99}},
        // L-shape
        {{0,0},{0,1},{1,1},{-99,-99},{-99,-99}},
        // T-shape
        {{0,0},{1,0},{2,0},{1,1},{-99,-99}},
        // single dot
        {{0,0},{-99,-99},{-99,-99},{-99,-99},{-99,-99}},
    };

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> nClusters(6, 10);
    std::uniform_int_distribution<int> shapeRoll(0, 5);
    std::uniform_int_distribution<int> xDist(2, kArenaW - 5);
    std::uniform_int_distribution<int> yDist(2, kArenaH - 5);

    const int cx = kArenaW / 2;
    const int cy = kArenaH / 2;
    const int nc = nClusters(rng);
    for (int c = 0; c < nc; ++c) {
        const int sx = xDist(rng);
        const int sy = yDist(rng);
        if (std::abs(sx - cx) < 3 && std::abs(sy - cy) < 3) continue;
        const int shape = shapeRoll(rng);
        for (int i = 0; i < 5; ++i) {
            const int dx = kShapes[shape][i][0];
            if (dx == -99) break;
            const int dy = kShapes[shape][i][1];
            g.Set(sx + dx, sy + dy, Tile::Wall);
        }
    }
    spawn = { kArenaW * 0.5f, kArenaH * 0.5f };
    const int px = int(spawn.x);
    const int py = int(spawn.y);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            g.Set(px + dx, py + dy, Tile::Floor);
}

// ---------------------------------------------------------------------------
// Collisions: circles vs grid walls.
// A unit world position (x, y) sits in tile (int(x), int(y)). For circle
// collision we just test the four tile cells the circle might overlap.
// ---------------------------------------------------------------------------
bool CircleBlockedByWalls(const Grid& g, float wx, float wy, float r) {
    const int minTx = int(std::floor(wx - r));
    const int maxTx = int(std::floor(wx + r));
    const int minTy = int(std::floor(wy - r));
    const int maxTy = int(std::floor(wy + r));
    for (int ty = minTy; ty <= maxTy; ++ty) {
        for (int tx = minTx; tx <= maxTx; ++tx) {
            if (g.At(tx, ty) != Tile::Wall) continue;
            // Closest point on the wall AABB to the circle center.
            const float cx = std::max(float(tx), std::min(wx, float(tx + 1)));
            const float cy = std::max(float(ty), std::min(wy, float(ty + 1)));
            const float dx = wx - cx;
            const float dy = wy - cy;
            if (dx * dx + dy * dy < r * r) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Entities.
// ---------------------------------------------------------------------------
struct Bullet {
    Vec2 pos;
    Vec2 vel;        // world units / s
    float life = 0.0f;
    bool fromPlayer = true;
    bool alive = true;
};

// Floating text that pops out of a hit/death spot and drifts upward while
// fading. Anchored in world space so the camera scroll keeps it tied to
// the spot, not glued to the screen. The struct carries its own text +
// colour + size + life so we can drive both KILL and CRIT popups (and
// future ones) through one pipeline.
// Coin drop. World-space (pos.x, pos.y) for floor-projection + z height in
// tiles. Once settled, vx/vy/vz are zero and it just sits waiting for pickup.
struct Coin {
    Vec2  pos{};
    Vec2  vel{};
    float z       = 0.0f;
    float vz      = 0.0f;
    float spin    = 0.0f;
    bool  settled = false;
    bool  alive   = true;
};

struct Popup {
    Vec2  pos{};
    char  text[24] = {};
    Color color = Color::White();
    float pxSize = 26.0f;
    float life   = 1.20f;
    float age    = 0.0f;
    bool  bold   = false;   // fake-bold: render text in a ring of offsets
    bool  alive  = true;
};

// One "star" of a sparkle burst — both the gold death star and the white
// bullet-vs-wall spark use this same struct. Per-particle life/size/color
// keeps EmitDeathStars and EmitWallSparks driving the same update + draw
// pipeline without forking into two particle systems.
struct StarParticle {
    Vec2  pos{};
    Vec2  vel{};
    float angle = 0.0f;
    float spin  = 0.0f;
    float age   = 0.0f;
    float life  = 0.55f;
    float size  = 30.0f;
    Color color{1.0f, 0.93f, 0.32f, 1.0f};
    bool  alive = true;
};

struct Enemy {
    Vec2 pos;
    Vec2 aim{1.0f, 0.0f};
    int  hp = kEnemyHpMax;
    float fireCd = 0.0f;
    float hurtFlash = 0.0f;
    bool alive = true;
    // Wander target re-rolled when reached or after wanderCd elapses, so
    // enemies strafe around instead of marching in a straight line.
    Vec2 wander{0.0f, 0.0f};
    float wanderCd = 0.0f;
    Vec2 knockback{0.0f, 0.0f};   // residual hit impulse, decays in UpdateEnemies
};

// ---------------------------------------------------------------------------
// Aim modes.
//   Auto  — only enemies that lie in the player's MOVEMENT direction (within
//           a 90° forward cone, in line-of-sight, within kAutoAimRange) are
//           lockable; closest such enemy is the target. Fire = bullet flies
//           at that target. No target in the cone → click does nothing (no
//           free shots in this mode, by design).
//   Mouse — original behaviour: aim follows the cursor, click fires straight
//           at the cursor.
// Press T to toggle. Default is Auto so newcomers don't have to find the key.
// ---------------------------------------------------------------------------
enum class AimMode : uint8_t { Auto, Mouse };

constexpr float kAutoAimRange     = 10.0f;   // world tiles
constexpr float kAutoAimConeCos   = 0.707f;  // ≈ cos(45°), so 90° full cone

// ---------------------------------------------------------------------------
// Ray-cast wall shadows.
//   A low-res visibility texture rebuilt every time the player moves: for
//   each sub-tile texel we ray-cast from the player to the texel's world
//   position, and write a multiplicative light value [0, 1]. Walls block
//   the ray (texel = kAmbient). Within line-of-sight the value falls off
//   quadratically from 1.0 at the player to kAmbient at kLightRange.
//   The texture rides Renderer2D's existing fog pipeline (R channel = light
//   multiplier) so no new shader / RHI path is needed; it works on both
//   D3D11 and Vulkan out of the box.
// ---------------------------------------------------------------------------
constexpr int   kShadowSubdiv = 24;    // texels per tile per axis — 24 makes
                                       // each texel ≈ 2×1.5 screen px so the
                                       // shadow staircase falls below the eye's
                                       // resolution after the bilinear filter.
                                       // Affordable thanks to the DDA-based
                                       // ShadowRayHitsWall below; the older
                                       // step=0.25 HasLOS would have made the
                                       // update too slow at this density.
constexpr float kLightRange   = 9.0f;  // world tiles; quadratic falloff
constexpr float kAmbient      = 0.08f; // floor in shadow tints to this — dark, but
                                       // not zero so silhouettes stay readable

// ---------------------------------------------------------------------------
// Shared context between scenes.
// ---------------------------------------------------------------------------
struct GameContext {
    Renderer2D* renderer = nullptr;
    MSDFFont*   font     = nullptr;
    RHIDevice*  device   = nullptr;
    int   score    = 0;
    int   wave     = 0;
    int   kills    = 0;
    int   playerHp = kPlayerHpMax;
    bool  newRun   = true;
    AimMode aimMode = AimMode::Auto;
    bool    shadowsOn = true;
    std::function<std::unique_ptr<Scene>()> makeArena;
    std::function<std::unique_ptr<Scene>()> makeTitle;
};

// ---------------------------------------------------------------------------
// Render helpers.
// ---------------------------------------------------------------------------
void DrawCenteredText(Renderer2D& r, MSDFFont& font, const std::string& s,
                      float y, float px, const Color& c) {
    const Vec2 sz = font.Measure(s, px);
    r.DrawText(font, s, {(kScreenW - sz.x) * 0.5f, y}, c, px);
}

// A rotated filled diamond/rect at a screen center. Used for the aim line.
void DrawScreenLine(Renderer2D& r, Vec2 a, Vec2 b, float thickness, const Color& c) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    const Vec2 mid{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
    r.DrawQuad(mid, {len, thickness}, std::atan2(dy, dx), c);
}

// Soft elliptical-looking shadow under each entity. Just two stacked rects.
void DrawShadow(Renderer2D& r, Vec2 screenFeet, float radiusPx) {
    r.DrawQuad({screenFeet.x - radiusPx, screenFeet.y - radiusPx * 0.30f},
               {radiusPx * 2.0f, radiusPx * 0.60f}, kShadow);
    r.DrawQuad({screenFeet.x - radiusPx * 0.7f, screenFeet.y - radiusPx * 0.20f},
               {radiusPx * 1.4f, radiusPx * 0.40f},
               Color(0.0f, 0.0f, 0.0f, 0.30f));
}

// ---------------------------------------------------------------------------
// Title scene.
// ---------------------------------------------------------------------------
class TitleScene : public Scene {
public:
    explicit TitleScene(GameContext* ctx) : m_ctx(ctx) {}
    void OnEnter() override { m_pulse = 0.0f; }
    void OnUpdate(float dt) override {
        m_pulse += dt;
        if (Input::IsKeyPressed(Key::Enter) || Input::IsKeyPressed(Key::Space)) {
            m_ctx->score    = 0;
            m_ctx->wave     = 0;
            m_ctx->kills    = 0;
            m_ctx->playerHp = kPlayerHpMax;
            m_ctx->newRun   = true;
            Manager().Change(m_ctx->makeArena());
        }
        if (Input::IsKeyPressed(Key::Escape)) {
            Application::Get().Close();
        }
    }
    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;
        r.Begin(Renderer2D::ScreenCamera(kScreenW, kScreenH));
        // Sky gradient — three horizontal bands.
        r.DrawQuad({0, 0},                 {kScreenW, kScreenH * 0.50f}, Color(0.12f, 0.14f, 0.22f, 1.0f));
        r.DrawQuad({0, kScreenH * 0.50f},  {kScreenW, kScreenH * 0.30f}, Color(0.18f, 0.32f, 0.28f, 1.0f));
        r.DrawQuad({0, kScreenH * 0.80f},  {kScreenW, kScreenH * 0.20f}, Color(0.10f, 0.20f, 0.18f, 1.0f));

        DrawCenteredText(r, *m_ctx->font, "BRAWL",   200.0f, 132.0f, Color(1.0f, 0.95f, 0.25f, 1.0f));
        DrawCenteredText(r, *m_ctx->font, "SHOOTER", 320.0f,  56.0f, Color(0.95f, 0.95f, 0.95f, 1.0f));

        const float blink = 0.6f + 0.4f * std::sin(m_pulse * 3.0f);
        DrawCenteredText(r, *m_ctx->font, "Press Enter to start",
                         480.0f, 32.0f, Color(1.0f, 1.0f, 1.0f, blink));
        DrawCenteredText(r, *m_ctx->font, "WASD move",
                         540.0f, 22.0f, Color(0.75f, 0.85f, 0.85f, 1.0f));
        DrawCenteredText(r, *m_ctx->font, "AUTO: auto-fire on lock   MOUSE: click to shoot",
                         574.0f, 22.0f, Color(1.00f, 0.85f, 0.30f, 1.0f));
        DrawCenteredText(r, *m_ctx->font, "T aim mode   H shadows   R rebuild   Esc back",
                         608.0f, 22.0f, Color(0.65f, 0.75f, 0.75f, 1.0f));
        r.End();
    }
private:
    GameContext* m_ctx = nullptr;
    float m_pulse = 0.0f;
};

// ---------------------------------------------------------------------------
// Arena scene — actual gameplay.
// ---------------------------------------------------------------------------
class ArenaScene : public Scene {
public:
    explicit ArenaScene(GameContext* ctx) : m_ctx(ctx) {}

    void OnEnter() override {
        m_rngArena.seed(std::random_device{}());
        Rebuild();
        m_playerHurt = 0.0f;
        m_fireCd     = 0.0f;
        m_bullets.clear();
        m_enemies.clear();
        m_particles.clear();
        m_popups.clear();
        m_coins.clear();
        m_waveTimer  = 0.0f;
        m_waveActive = false;
        m_ctx->wave  = 0;
        m_facing     = {1.0f, 0.0f};
        m_moveDir    = {1.0f, 0.0f};
        m_playerKnockback = {0.0f, 0.0f};
        m_autoLocked = false;
        m_autoPulse  = 0.0f;
        m_modeFlash  = 0.0f;
        m_shadowFlash = 0.0f;
        m_lastShadowSrc = {1e9f, 1e9f};
        m_dead = false;
        m_deathPulse = 0.0f;
        m_paused = false;
        m_pausePulse = 0.0f;
        EnsureShadowTexture();
        EnsureStarTexture();
        EnsureCoinSprite();
    }

    // Load the coin sprite sheet once and cache TextureRegions for each of
    // the 10 frames. The frame origins come straight from the artist's
    // layout (uneven 8 / 42 px gaps), so we don't try to derive them by
    // dividing the texture size.
    void EnsureCoinSprite() {
        if (m_coinTex || !m_ctx->device) return;
        const std::string path =
            std::string(NARAGAME_ASSETS_DIR) + "/coin.png";
        m_coinTex = LoadTexture(*m_ctx->device, path);
        if (!m_coinTex) {
            NARAGAME_WARN("brawl: coin sprite '%s' missing — coins will be invisible",
                          path.c_str());
            return;
        }
        // (x, y) top-left of each 67×67 frame, in PNG pixel coords.
        static const float kOrigins[kCoinFrameCount][2] = {
            { 24.0f,  42.0f}, { 99.0f,  42.0f}, {174.0f,  42.0f},
            {249.0f,  42.0f}, {324.0f,  42.0f},
            { 24.0f, 151.0f}, { 99.0f, 151.0f}, {174.0f, 151.0f},
            {249.0f, 151.0f}, {324.0f, 151.0f},
        };
        for (int i = 0; i < kCoinFrameCount; ++i) {
            m_coinFrames[i] = TextureRegion::FromPixels(
                m_coinTex.get(),
                kOrigins[i][0], kOrigins[i][1],
                kCoinFrameW,   kCoinFrameH);
        }
    }

    // Bake the sparkle sprite into an RGBA8 texture. Layout: a Gaussian-ish
    // bright core + cos²(2θ)^n "ray" modulation (4 main rays at 0/π/2 and
    // 4 dimmer rays rotated π/4 between them). Rays fade with radius so
    // the sprite reaches alpha 0 at the texture edge — drawn rotated, the
    // soft falloff hides any sampling aliasing.
    void EnsureStarTexture() {
        if (m_starTex) return;
        constexpr int kN = 128;
        std::vector<uint8_t> px(size_t(kN) * kN * 4);
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                const float fx = (float(x) + 0.5f) / float(kN) * 2.0f - 1.0f;
                const float fy = (float(y) + 0.5f) / float(kN) * 2.0f - 1.0f;
                const float r2 = fx * fx + fy * fy;
                float intensity = 0.0f;
                if (r2 < 1.0f) {
                    const float r     = std::sqrt(r2);
                    const float core  = std::exp(-r2 * 7.0f);
                    const float theta = std::atan2(fy, fx);
                    const float ray4  = std::pow(std::fabs(std::cos(theta * 2.0f)), 20.0f);
                    const float ray4d = std::pow(std::fabs(std::cos(theta * 2.0f - 0.78539816f)), 32.0f);
                    const float fade  = 1.0f - r;
                    intensity = core + (ray4 * 0.90f + ray4d * 0.55f) * fade;
                    if (intensity > 1.0f) intensity = 1.0f;
                }
                const uint8_t v = uint8_t(intensity * 255.0f + 0.5f);
                const size_t i = (size_t(y) * kN + x) * 4;
                px[i + 0] = v;
                px[i + 1] = v;
                px[i + 2] = v;
                px[i + 3] = v;
            }
        }
        TextureDesc td;
        td.width  = kN;
        td.height = kN;
        td.format = TextureFormat::RGBA8;
        td.filter = TextureFilter::Linear;
        td.pixels = px.data();
        if (m_ctx->device)
            m_starTex = m_ctx->device->CreateTexture(td);
        if (m_starTex)
            m_starRegion = TextureRegion::FromPixels(
                m_starTex.get(), 0.0f, 0.0f, float(kN), float(kN));
    }

    // Spawn 4 gold star particles erupting radially from `at`. Used on enemy
    // death; we randomise the base angle a bit so consecutive kills don't
    // all spit out an identical cross.
    void EmitDeathStars(Vec2 at) {
        std::uniform_real_distribution<float> jitter(0.0f, 1.5707963f);
        const float base = jitter(m_rng);
        for (int i = 0; i < 4; ++i) {
            const float a = base + i * 1.5707963f;          // 90° apart
            StarParticle p;
            p.pos   = at;
            p.vel   = { std::cos(a) * kStarSpeed,
                        std::sin(a) * kStarSpeed };
            p.angle = a;
            p.spin  = (i & 1) ? -kStarSpin : kStarSpin;
            p.life  = kStarLife;
            p.size  = kStarPx;
            p.color = Color(1.0f, 0.93f, 0.32f, 1.0f);
            m_particles.push_back(p);
        }
    }

    // Spawn 2-4 coins erupting from `at`, each with a random radial speed
    // and upward vz so the burst arcs out in a small fountain.
    void EmitCoins(Vec2 at) {
        std::uniform_int_distribution<int>   countDist(kCoinMinPerKill, kCoinMaxPerKill);
        std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> vzDist(kCoinInitVzMin, kCoinInitVzMax);
        std::uniform_real_distribution<float> vxyDist(kCoinInitVxyMin, kCoinInitVxyMax);
        const int n = countDist(m_rng);
        for (int i = 0; i < n; ++i) {
            const float a   = angle(m_rng);
            const float vxy = vxyDist(m_rng);
            Coin c;
            c.pos  = at;
            c.vel  = { std::cos(a) * vxy, std::sin(a) * vxy };
            c.vz   = vzDist(m_rng);
            c.spin = angle(m_rng);
            m_coins.push_back(c);
        }
    }

    // KILL count popup — yellow, normal size, drifts up from the kill spot.
    void EmitKillPopup(Vec2 at, int number) {
        Popup p;
        p.pos = at;
        std::snprintf(p.text, sizeof(p.text), "KILL %d", number);
        p.color  = Color(1.0f, 0.93f, 0.32f, 1.0f);
        p.pxSize = kPopupPx;
        p.life   = kPopupLife;
        m_popups.push_back(p);
    }

    // CRIT! popup — red-orange, bigger, spawned slightly higher than the
    // KILL popup (world Y offset −0.4 tile ≈ 14 screen px) so when a crit
    // also kills the two read as a stacked pair instead of overlapping.
    void EmitCritPopup(Vec2 at) {
        Popup p;
        p.pos    = { at.x, at.y - 0.4f };
        std::snprintf(p.text, sizeof(p.text), "CRITICAL");
        p.color  = Color(1.00f, 0.42f, 0.22f, 1.0f);
        p.pxSize = kPopupPx * 1.45f;
        p.life   = kPopupLife;
        p.bold   = true;
        m_popups.push_back(p);
    }

    // Bullet-vs-wall spark: 5 small white sparkles flying back along the
    // bullet's incoming direction within a wide cone. Short life, small
    // sprite, fast spin — reads as "pang!" not as a death burst.
    void EmitWallSparks(Vec2 at, Vec2 bulletVel) {
        const float v2 = bulletVel.x * bulletVel.x + bulletVel.y * bulletVel.y;
        if (v2 < 1e-6f) return;
        const float backAng = std::atan2(-bulletVel.y, -bulletVel.x);
        std::uniform_real_distribution<float> spread(-1.0f, 1.0f);     // ~±57°
        std::uniform_real_distribution<float> spd(2.0f, 4.0f);
        std::uniform_real_distribution<float> rot(-3.14159f, 3.14159f);
        constexpr int kCount = 5;
        for (int i = 0; i < kCount; ++i) {
            const float a = backAng + spread(m_rng);
            const float s = spd(m_rng);
            StarParticle p;
            p.pos   = at;
            p.vel   = { std::cos(a) * s, std::sin(a) * s };
            p.angle = rot(m_rng);
            p.spin  = (i & 1) ? -14.0f : 14.0f;
            p.life  = 0.25f;
            p.size  = 11.0f;
            p.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
            m_particles.push_back(p);
        }
    }

    // Allocate (or reallocate, if subdiv ever changes) the shadow texture
    // sized to the current arena. Called once on scene entry; UpdateShadows
    // only ever re-uploads pixels via UpdatePixels(), never recreates.
    void EnsureShadowTexture() {
        m_shadowW = kArenaW * kShadowSubdiv;
        m_shadowH = kArenaH * kShadowSubdiv;
        m_shadowPixels.assign(size_t(m_shadowW) * m_shadowH * 4, 255);
        TextureDesc td;
        td.width  = uint32_t(m_shadowW);
        td.height = uint32_t(m_shadowH);
        td.format = TextureFormat::RGBA8;
        td.filter = TextureFilter::Linear;
        td.pixels = m_shadowPixels.data();
        if (m_ctx->device)
            m_shadowTex = m_ctx->device->CreateTexture(td);
    }

    void OnUpdate(float dt) override {
        // Esc means different things in different states:
        //   - dead    → quit to title (matches the death overlay's hint)
        //   - paused  → resume (toggle off)
        //   - playing → pause (toggle on)
        if (Input::IsKeyPressed(Key::Escape)) {
            if (m_dead) {
                Manager().Change(m_ctx->makeTitle());
                return;
            }
            m_paused = !m_paused;
            m_pausePulse = 0.0f;
        }

        m_shadowFlash = std::max(0.0f, m_shadowFlash - dt);

        // Once dead we freeze gameplay updates but keep particles running
        // and listen for restart input.
        if (m_dead) {
            m_deathPulse += dt;
            UpdateParticles(dt);
            UpdatePopups(dt);
            UpdateCoins(dt, /*pickupEnabled=*/false);
            if (Input::IsKeyPressed(Key::Enter) || Input::IsKeyPressed(Key::Space)) {
                m_ctx->playerHp = kPlayerHpMax;
                m_ctx->score    = 0;
                m_ctx->wave     = 0;
                m_ctx->kills    = 0;
                Manager().Change(m_ctx->makeArena());
            }
            return;
        }

        // Paused: everything freezes (no enemy AI, no bullets, no spawns,
        // no shadow rebuild). Only the pulse-blink animation and Q-to-quit
        // listen for input.
        if (m_paused) {
            m_pausePulse += dt;
            if (Input::IsKeyPressed(Key::Q)) {
                Manager().Change(m_ctx->makeTitle());
            }
            return;
        }

        // Gameplay-only inputs.
        if (Input::IsKeyPressed(Key::R)) {
            Rebuild();
            m_bullets.clear();
            m_enemies.clear();
            m_waveTimer  = 0.0f;
            m_waveActive = false;
            m_ctx->wave  = 0;
            return;
        }
        if (Input::IsKeyPressed(Key::H)) {
            m_ctx->shadowsOn = !m_ctx->shadowsOn;
            m_shadowFlash = 0.8f;
            m_lastShadowSrc = {1e9f, 1e9f};   // force a fresh upload when re-enabled
        }

        UpdateCameraToPlayer();
        UpdatePlayer(dt);
        UpdateBullets(dt);
        UpdateEnemies(dt);
        UpdateParticles(dt);
        UpdatePopups(dt);
        UpdateCoins(dt, /*pickupEnabled=*/true);
        UpdateWaves(dt);
        if (m_ctx->shadowsOn) UpdateShadows();

        if (m_ctx->playerHp <= 0) {
            m_dead = true;
            m_deathPulse = 0.0f;
        }
    }

    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;
        r.Begin(Renderer2D::ScreenCamera(kScreenW, kScreenH));
        r.DrawQuad({0, 0}, {kScreenW, kScreenH}, kBgClear);

        // Only the FLOOR pass uses the fog/shadow texture — for it the
        // screen-Y → world-Y inverse maps cleanly back to its own tile. Walls
        // and entities don't, because their sprites extend upward in screen
        // space and would sample texels for tiles that aren't theirs (walls
        // pick up the dark of the floor behind them; entities in shadow pick
        // up the bright of the wall in front of them). They both tint
        // themselves manually instead — walls via per-sub-quad distance,
        // entities via SampleShadowAt on their feet position.
        const bool fogActive = m_ctx->shadowsOn && m_shadowTex != nullptr;
        if (fogActive) {
            const float invW = 1.0f / (kArenaW * kTileW);
            const float invH = 1.0f / (kArenaH * kTileH);
            const Vec4 uRow{ invW, 0.0f, -m_camOrigin.x * invW, 0.0f };
            const Vec4 vRow{ 0.0f, invH, -m_camOrigin.y * invH, 0.0f };
            r.SetFog(m_shadowTex.get(), uRow, vRow);
        }
        DrawArenaFloor(r);
        if (fogActive) r.SetFog(nullptr, {}, {});
        DrawSorted(r, fogActive);
        DrawParticles(r);
        DrawPopups(r);
        DrawAutoAimOverlay(r);
        DrawHud(r);
        if (m_paused) DrawPauseOverlay(r);
        if (m_dead)   DrawDeathOverlay(r);

        r.End();
    }

private:
    // ----- Setup -----
    void Rebuild() {
        BuildArena(m_grid, m_rngArena(), m_playerSpawn);
        m_player = m_playerSpawn;
    }

    // The camera origin is the screen position of world (0,0). We want the
    // player to read near screen-center but clamp so the camera doesn't show
    // void past the arena edges.
    void UpdateCameraToPlayer() {
        const Vec2 wantedOrigin{
            kScreenW * 0.5f - m_player.x * kTileW,
            kScreenH * 0.5f - m_player.y * kTileH,
        };
        // Clamp so the arena fills the screen if possible.
        const float arenaPxW = kArenaW * kTileW;
        const float arenaPxH = kArenaH * kTileH + kWallScreenH;
        float minX = std::min(0.0f, kScreenW - arenaPxW);
        float maxX = 0.0f;
        if (arenaPxW < kScreenW) { minX = (kScreenW - arenaPxW) * 0.5f; maxX = minX; }
        float minY = std::min(60.0f, kScreenH - arenaPxH);  // 60 px top HUD reserve
        float maxY = 60.0f;
        if (arenaPxH < kScreenH - 60.0f) { minY = (kScreenH - arenaPxH) * 0.5f; maxY = minY; }
        m_camOrigin.x = std::max(minX, std::min(maxX, wantedOrigin.x));
        m_camOrigin.y = std::max(minY, std::min(maxY, wantedOrigin.y));
    }

    // Read the shadow texture on the CPU at a single world point. Entities
    // tint themselves by this so their sprite — which extends upward off
    // their feet and would otherwise sample texels for *other* tiles via
    // SetFog's screen-Y → world-Y inverse — uses one value tied to their
    // actual position. Point-sampled; one tint per sprite is what we want.
    float SampleShadowAt(Vec2 wp) const {
        if (!m_ctx->shadowsOn || m_shadowPixels.empty()) return 1.0f;
        const int x = int(wp.x * kShadowSubdiv);
        const int y = int(wp.y * kShadowSubdiv);
        if (x < 0 || y < 0 || x >= m_shadowW || y >= m_shadowH) return 1.0f;
        return m_shadowPixels[(size_t(y) * m_shadowW + x) * 4] / 255.0f;
    }

    // Distance-only visibility: a wall outside the light disc is hidden, a
    // wall inside it tints with the same quadratic falloff the floor uses,
    // and crucially no LOS check — so walls behind other walls still draw
    // (the user wants the "what's around me" silhouette, not a flashlight
    // cone). Returning 0 means "skip this wall entirely".
    float WallLight(int tx, int ty) const {
        const float dx = float(tx) + 0.5f - m_player.x;
        const float dy = float(ty) + 0.5f - m_player.y;
        const float d2 = dx * dx + dy * dy;
        const float r2 = kLightRange * kLightRange;
        if (d2 >= r2) return 0.0f;
        return 1.0f - d2 / r2;
    }

    // Proper grid-DDA "does this ray hit a wall before reaching (bx, by)?"
    // Visits exactly the tiles the ray crosses, so the work is O(|Δtx|+|Δty|)
    // instead of HasLOS's O(length / 0.25) step-walk — roughly 3× faster for
    // the long rays the shadow update fires every frame. Bracketed by
    // start/end so a ray that begins or ends inside a wall still answers
    // correctly. Returns true if the path is blocked.
    bool ShadowRayHitsWall(Vec2 a, Vec2 b) const {
        int tx = int(a.x), ty = int(a.y);
        const int bx = int(b.x), by = int(b.y);
        if (m_grid.At(tx, ty) == Tile::Wall) return true;
        if (tx == bx && ty == by) return false;
        const float dx = b.x - a.x, dy = b.y - a.y;
        const int sx = (dx > 0) ? 1 : -1;
        const int sy = (dy > 0) ? 1 : -1;
        const float ix = (dx == 0.0f) ? 1e18f : std::fabs(1.0f / dx);
        const float iy = (dy == 0.0f) ? 1e18f : std::fabs(1.0f / dy);
        // Distance (in ray-t) to the next x / y grid line ahead of the start.
        float nextX = (sx > 0) ? (float(tx + 1) - a.x) * ix
                               : (a.x - float(tx))     * ix;
        float nextY = (sy > 0) ? (float(ty + 1) - a.y) * iy
                               : (a.y - float(ty))     * iy;
        const int maxSteps = std::abs(bx - tx) + std::abs(by - ty) + 2;
        for (int i = 0; i < maxSteps; ++i) {
            if (nextX < nextY) { tx += sx; nextX += ix; }
            else               { ty += sy; nextY += iy; }
            if (tx == bx && ty == by) return false;
            if (m_grid.At(tx, ty) == Tile::Wall) return true;
        }
        return false;
    }

    // Rebuild the shadow texture from the current player position. Walks
    // every sub-tile texel and writes a light multiplier in [0, 1]:
    //   * Wall texel               → 1.0 (so neighbouring floor isn't dimmed
    //                                 by linear-filter bleed at the wall edge)
    //   * Floor texel, LOS clear   → max(ambient, 1 - (d/R)²)
    //   * Floor texel, LOS blocked → ambient (visible but dim)
    // The ray-cast is the same HasLOS the rest of the gameplay uses, so the
    // shadow boundary matches what enemies see and shoot through. We skip
    // entirely when the player hasn't moved much since the last upload —
    // most frames are no-ops while the player is reading the map.
    void UpdateShadows() {
        if (!m_shadowTex) return;
        const Vec2 src = m_player;
        const float dx0 = src.x - m_lastShadowSrc.x;
        const float dy0 = src.y - m_lastShadowSrc.y;
        if (dx0 * dx0 + dy0 * dy0 < 0.01f * 0.01f) return;   // ~0.01 tile threshold
        m_lastShadowSrc = src;

        const float invR2 = 1.0f / (kLightRange * kLightRange);
        const float inv   = 1.0f / float(kShadowSubdiv);
        for (int sy = 0; sy < m_shadowH; ++sy) {
            for (int sx = 0; sx < m_shadowW; ++sx) {
                const float wx = (sx + 0.5f) * inv;
                const float wy = (sy + 0.5f) * inv;
                float light;
                if (m_grid.At(int(wx), int(wy)) == Tile::Wall) {
                    light = 1.0f;
                } else {
                    const float dx = wx - src.x;
                    const float dy = wy - src.y;
                    const float d2 = dx * dx + dy * dy;
                    if (!ShadowRayHitsWall(src, {wx, wy})) {
                        float fall = 1.0f - d2 * invR2;
                        if (fall < 0.0f) fall = 0.0f;
                        light = (fall > kAmbient) ? fall : kAmbient;
                    } else {
                        light = kAmbient;
                    }
                }
                const uint8_t v = uint8_t(light * 255.0f + 0.5f);
                const size_t idx = (size_t(sy) * m_shadowW + sx) * 4;
                m_shadowPixels[idx + 0] = v;
                m_shadowPixels[idx + 1] = v;
                m_shadowPixels[idx + 2] = v;
                m_shadowPixels[idx + 3] = 255;
            }
        }
        m_shadowTex->UpdatePixels(m_shadowPixels.data());
    }

    // Scan all live enemies and return the closest one inside the auto-aim
    // cone (90° forward from m_moveDir), within range, and with LOS. Returns
    // null when nothing qualifies — the caller treats that as "no lock".
    const Enemy* FindAutoTarget() const {
        const Enemy* best = nullptr;
        float bestD2 = kAutoAimRange * kAutoAimRange;
        for (const Enemy& e : m_enemies) {
            if (!e.alive) continue;
            const float dx = e.pos.x - m_player.x;
            const float dy = e.pos.y - m_player.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > bestD2 || d2 < 1e-6f) continue;
            const float len = std::sqrt(d2);
            const float dot = (dx * m_moveDir.x + dy * m_moveDir.y) / len;
            if (dot < kAutoAimConeCos) continue;
            if (!HasLOS(m_player, e.pos)) continue;
            bestD2 = d2;
            best = &e;
        }
        return best;
    }

    // ----- Update -----
    void UpdatePlayer(float dt) {
        m_playerHurt = std::max(0.0f, m_playerHurt - dt);
        m_fireCd     = std::max(0.0f, m_fireCd - dt);
        m_autoPulse += dt;
        m_modeFlash  = std::max(0.0f, m_modeFlash - dt);

        // Aim-mode toggle (T). Persists across deaths via GameContext.
        if (Input::IsKeyPressed(Key::T)) {
            m_ctx->aimMode = (m_ctx->aimMode == AimMode::Auto)
                                 ? AimMode::Mouse : AimMode::Auto;
            m_modeFlash = 0.8f;
        }

        // Movement. WASD is locked out while the player is being knocked
        // back from a bullet hit — the impulse should feel like a yank, not
        // something you can cancel by holding the opposite direction. The
        // facing / auto-aim / shooting paths below keep working so the
        // player can still aim and return fire mid-slide.
        const bool kbActive = (m_playerKnockback.x * m_playerKnockback.x +
                               m_playerKnockback.y * m_playerKnockback.y > 0.01f);
        Vec2 input{0.0f, 0.0f};
        if (!kbActive) {
            if (Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::Left))  input.x -= 1.0f;
            if (Input::IsKeyDown(Key::D) || Input::IsKeyDown(Key::Right)) input.x += 1.0f;
            if (Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::Up))    input.y -= 1.0f;
            if (Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::Down))  input.y += 1.0f;
        }
        const float ilen = std::sqrt(input.x * input.x + input.y * input.y);
        if (ilen > 0.001f) {
            input = { input.x / ilen, input.y / ilen };
            // Latch the latest movement direction. The auto-aim cone keeps
            // pointing this way after the player stops, so a quick tap-stop-
            // shoot still fires "ahead" of the run.
            m_moveDir = input;
        }

        const float step = kPlayerSpeed * dt;
        const float nx = m_player.x + input.x * step;
        const float ny = m_player.y + input.y * step;
        // Axis-separated collision so the player can slide along walls.
        if (!CircleBlockedByWalls(m_grid, nx, m_player.y, kPlayerR)) m_player.x = nx;
        if (!CircleBlockedByWalls(m_grid, m_player.x, ny, kPlayerR)) m_player.y = ny;

        // Bullet-hit knockback: apply residual impulse with wall slide, then
        // bleed it off exponentially so it fades within ~0.5s. The threshold
        // prevents floating-point dust from carrying movement forever.
        if (m_playerKnockback.x * m_playerKnockback.x +
            m_playerKnockback.y * m_playerKnockback.y > 0.01f) {
            const float kx = m_playerKnockback.x * dt;
            const float ky = m_playerKnockback.y * dt;
            if (!CircleBlockedByWalls(m_grid, m_player.x + kx, m_player.y, kPlayerR))
                m_player.x += kx;
            if (!CircleBlockedByWalls(m_grid, m_player.x, m_player.y + ky, kPlayerR))
                m_player.y += ky;
            const float decay = std::exp(-kKnockbackDecay * dt);
            m_playerKnockback.x *= decay;
            m_playerKnockback.y *= decay;
        } else {
            m_playerKnockback = {0.0f, 0.0f};
        }

        // Aim. Branches on the active mode; both modes write m_facing so the
        // gun barrel + bullet spawn share a single code path below.
        m_autoLocked = false;
        if (m_ctx->aimMode == AimMode::Auto) {
            const Enemy* tgt = FindAutoTarget();
            if (tgt) {
                const float dx = tgt->pos.x - m_player.x;
                const float dy = tgt->pos.y - m_player.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                m_facing = { dx / len, dy / len };
                m_autoLocked    = true;
                m_autoTargetPos = tgt->pos;
            } else {
                // No enemy in the cone — point the gun the way we're heading
                // so the sprite reads naturally. We won't shoot in this state.
                m_facing = m_moveDir;
            }
        } else {
            const Vec2 mouseScreen = Input::MousePosition();
            const Vec2 mouseWorld  = S2W(mouseScreen, m_camOrigin);
            Vec2 aim = { mouseWorld.x - m_player.x, mouseWorld.y - m_player.y };
            const float alen = std::sqrt(aim.x * aim.x + aim.y * aim.y);
            if (alen > 0.001f) m_facing = { aim.x / alen, aim.y / alen };
        }

        // Shoot. Mouse mode: hold left-click to fire toward the cursor (click
        // is the only trigger). Auto mode: NO input required — the gun fires
        // by itself whenever there's a lock, gated only by the fire cooldown.
        // Without a lock there's no target, so auto-fire pauses.
        const bool wantFire = (m_ctx->aimMode == AimMode::Auto)
                                  ? m_autoLocked
                                  : Input::IsMouseDown(MouseButton::Left);
        if (wantFire && m_fireCd <= 0.0f) {
            m_fireCd = kPlayerFireCd;
            Bullet b;
            // Spawn at gun tip rather than the player's center so the bullet
            // doesn't appear out of the body sprite.
            b.pos = { m_player.x + m_facing.x * 0.55f,
                      m_player.y + m_facing.y * 0.55f };
            b.vel = { m_facing.x * kPlayerBulletV,
                      m_facing.y * kPlayerBulletV };
            b.fromPlayer = true;
            m_bullets.push_back(b);
        }
    }

    void UpdateBullets(float dt) {
        for (Bullet& b : m_bullets) {
            if (!b.alive) continue;
            b.life += dt;
            if (b.life >= kBulletLife) { b.alive = false; continue; }
            const float nx = b.pos.x + b.vel.x * dt;
            const float ny = b.pos.y + b.vel.y * dt;
            // Walls stop bullets — and throw a white spark burst.
            if (CircleBlockedByWalls(m_grid, nx, ny, kBulletR)) {
                EmitWallSparks(b.pos, b.vel);
                b.alive = false; continue;
            }
            b.pos.x = nx;
            b.pos.y = ny;
            // Hit tests.
            if (b.fromPlayer) {
                for (Enemy& e : m_enemies) {
                    if (!e.alive) continue;
                    const float dx = e.pos.x - b.pos.x;
                    const float dy = e.pos.y - b.pos.y;
                    const float rr = (e.hp > 0 ? kEnemyR + kBulletR : 0.0f);
                    if (dx * dx + dy * dy < rr * rr) {
                        std::uniform_real_distribution<float> roll(0.0f, 1.0f);
                        const bool isCrit = roll(m_rng) < kCritChance;
                        const int  dmg = isCrit ? kPlayerBulletDmg * kCritMult
                                                : kPlayerBulletDmg;
                        e.hp -= dmg;
                        e.hurtFlash = kEnemyHurtFlash;
                        // Knockback along the bullet's normalised flight dir.
                        const float vlen = std::sqrt(b.vel.x * b.vel.x +
                                                     b.vel.y * b.vel.y);
                        if (vlen > 0.001f) {
                            const float s = (isCrit ? 1.4f : 1.0f) *
                                            kEnemyKnockbackV / vlen;
                            e.knockback.x = b.vel.x * s;
                            e.knockback.y = b.vel.y * s;
                        }
                        b.alive = false;
                        if (isCrit) EmitCritPopup(e.pos);
                        if (e.hp <= 0) {
                            e.alive = false;
                            m_ctx->score += kScorePerKill;
                            ++m_ctx->kills;
                            EmitDeathStars(e.pos);
                            EmitKillPopup(e.pos, m_ctx->kills);
                            EmitCoins(e.pos);
                        }
                        break;
                    }
                }
            } else {
                const float dx = m_player.x - b.pos.x;
                const float dy = m_player.y - b.pos.y;
                const float rr = kPlayerR + kBulletR;
                if (dx * dx + dy * dy < rr * rr && m_playerHurt <= 0.0f) {
                    m_ctx->playerHp -= kEnemyBulletDmg;
                    m_playerHurt = kPlayerHurtIFrame;
                    const float vlen = std::sqrt(b.vel.x * b.vel.x +
                                                 b.vel.y * b.vel.y);
                    if (vlen > 0.001f) {
                        const float s = kPlayerKnockbackV / vlen;
                        m_playerKnockback.x = b.vel.x * s;
                        m_playerKnockback.y = b.vel.y * s;
                    }
                    b.alive = false;
                }
            }
        }
        m_bullets.erase(std::remove_if(m_bullets.begin(), m_bullets.end(),
                                       [](const Bullet& b) { return !b.alive; }),
                        m_bullets.end());
    }

    void UpdateEnemies(float dt) {
        for (Enemy& e : m_enemies) {
            if (!e.alive) continue;
            e.fireCd    = std::max(0.0f, e.fireCd - dt);
            e.hurtFlash = std::max(0.0f, e.hurtFlash - dt);
            e.wanderCd  = std::max(0.0f, e.wanderCd - dt);

            const float dx0 = m_player.x - e.pos.x;
            const float dy0 = m_player.y - e.pos.y;
            const float dist = std::sqrt(dx0 * dx0 + dy0 * dy0);
            if (dist > 0.001f) e.aim = { dx0 / dist, dy0 / dist };

            // Behaviour:
            //   too close (< retreat range) → back away
            //   inside shoot range          → strafe + shoot
            //   far                         → chase straight
            Vec2 wantDir{0, 0};
            if (dist < kEnemyRetreatRange && dist > 0.001f) {
                wantDir = { -e.aim.x, -e.aim.y };
            } else if (dist < kEnemyShootRange) {
                // Pick or refresh a wander target perpendicular to aim, so the
                // enemy circle-strafes.
                if (e.wanderCd <= 0.0f) {
                    e.wanderCd = kEnemyWanderCd;
                    std::uniform_int_distribution<int> sign(0, 1);
                    const float s = sign(m_rng) ? 1.0f : -1.0f;
                    e.wander = { -e.aim.y * s, e.aim.x * s };
                }
                wantDir = e.wander;
            } else if (dist > 0.001f) {
                wantDir = e.aim;
            }

            const float step = kEnemySpeed * dt;
            const float nx = e.pos.x + wantDir.x * step;
            const float ny = e.pos.y + wantDir.y * step;
            if (!CircleBlockedByWalls(m_grid, nx, e.pos.y, kEnemyR)) e.pos.x = nx;
            if (!CircleBlockedByWalls(m_grid, e.pos.x, ny, kEnemyR)) e.pos.y = ny;

            // Bullet-hit knockback (same axis-separated slide + exp decay as
            // the player). Runs after the AI's own move so a heavy impulse
            // briefly overrides the chase direction.
            if (e.knockback.x * e.knockback.x +
                e.knockback.y * e.knockback.y > 0.01f) {
                const float kx = e.knockback.x * dt;
                const float ky = e.knockback.y * dt;
                if (!CircleBlockedByWalls(m_grid, e.pos.x + kx, e.pos.y, kEnemyR))
                    e.pos.x += kx;
                if (!CircleBlockedByWalls(m_grid, e.pos.x, e.pos.y + ky, kEnemyR))
                    e.pos.y += ky;
                const float decay = std::exp(-kKnockbackDecay * dt);
                e.knockback.x *= decay;
                e.knockback.y *= decay;
            } else {
                e.knockback = {0.0f, 0.0f};
            }

            // Shoot if within range and a rough LOS (no wall on the segment).
            if (dist < kEnemyShootRange && e.fireCd <= 0.0f && HasLOS(e.pos, m_player)) {
                e.fireCd = kEnemyFireCd;
                Bullet b;
                b.pos = { e.pos.x + e.aim.x * 0.55f, e.pos.y + e.aim.y * 0.55f };
                b.vel = { e.aim.x * kEnemyBulletV,   e.aim.y * kEnemyBulletV   };
                b.fromPlayer = false;
                m_bullets.push_back(b);
            }
        }
        m_enemies.erase(std::remove_if(m_enemies.begin(), m_enemies.end(),
                                       [](const Enemy& e) { return !e.alive; }),
                        m_enemies.end());
    }

    // pickupEnabled lets the dead branch keep coins bouncing without letting
    // the dead player magically vacuum them.
    void UpdateCoins(float dt, bool pickupEnabled) {
        for (Coin& c : m_coins) {
            if (!c.alive) continue;
            if (!c.settled) {
                c.vz   -= kCoinGravity * dt;
                // Axis-separated wall slide + bounce: a coin that would push
                // into a wall on the X axis stays put on X and its vx flips
                // (with damping). Same on Y. Without this coins can fly
                // through walls and settle inside unreachable tiles.
                const float nx = c.pos.x + c.vel.x * dt;
                if (CircleBlockedByWalls(m_grid, nx, c.pos.y, kCoinR))
                    c.vel.x = -c.vel.x * kCoinWallBounce;
                else
                    c.pos.x = nx;
                const float ny = c.pos.y + c.vel.y * dt;
                if (CircleBlockedByWalls(m_grid, c.pos.x, ny, kCoinR))
                    c.vel.y = -c.vel.y * kCoinWallBounce;
                else
                    c.pos.y = ny;
                c.z    += c.vz * dt;
                c.spin += kCoinSpinSpeed * dt;
                if (c.z <= 0.0f) {
                    c.z = 0.0f;
                    if (-c.vz < kCoinSettleVz) {
                        c.settled = true;
                        c.vz  = 0.0f;
                        c.vel = {0.0f, 0.0f};
                    } else {
                        c.vz    = -c.vz * kCoinBounce;
                        c.vel.x *= kCoinFriction;
                        c.vel.y *= kCoinFriction;
                    }
                }
            }
            if (pickupEnabled) {
                const float dx = c.pos.x - m_player.x;
                const float dy = c.pos.y - m_player.y;
                if (dx * dx + dy * dy < kCoinPickupR * kCoinPickupR) {
                    m_ctx->score += kCoinScore;
                    c.alive = false;
                }
            }
        }
        m_coins.erase(std::remove_if(m_coins.begin(), m_coins.end(),
                                     [](const Coin& c) { return !c.alive; }),
                      m_coins.end());
    }

    void UpdatePopups(float dt) {
        for (Popup& p : m_popups) {
            if (!p.alive) continue;
            p.age += dt;
            if (p.age >= p.life) { p.alive = false; continue; }
            p.pos.y -= kPopupRise * dt;     // float upward (south→north)
        }
        m_popups.erase(std::remove_if(m_popups.begin(), m_popups.end(),
                                      [](const Popup& p) { return !p.alive; }),
                       m_popups.end());
    }

    void UpdateParticles(float dt) {
        for (StarParticle& p : m_particles) {
            if (!p.alive) continue;
            p.age += dt;
            if (p.age >= p.life) { p.alive = false; continue; }
            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;
            const float drag = std::exp(-kStarDrag * dt);
            p.vel.x *= drag;
            p.vel.y *= drag;
            p.angle += p.spin * dt;
        }
        m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
                                         [](const StarParticle& p) { return !p.alive; }),
                          m_particles.end());
    }

    void UpdateWaves(float dt) {
        if (!m_waveActive) {
            if (m_enemies.empty()) {
                m_waveTimer += dt;
                if (m_waveTimer >= kBetweenWaves) {
                    m_waveTimer = 0.0f;
                    m_waveActive = true;
                    SpawnNextWave();
                }
            }
        } else {
            // Wave finishes the instant the last enemy dies — handled by the
            // empty-list check above, which then waits kBetweenWaves before
            // the next spawn.
            if (m_enemies.empty()) m_waveActive = false;
        }
    }

    void SpawnNextWave() {
        ++m_ctx->wave;
        const int n = std::min(kMaxWaveSize,
                               kFirstWaveSize + kWaveIncrement * (m_ctx->wave - 1));
        std::uniform_int_distribution<int> xDist(2, kArenaW - 3);
        std::uniform_int_distribution<int> yDist(2, kArenaH - 3);
        int placed = 0;
        int tries  = 0;
        while (placed < n && tries < n * 30) {
            ++tries;
            const float ex = xDist(m_rng) + 0.5f;
            const float ey = yDist(m_rng) + 0.5f;
            const float dx = ex - m_player.x;
            const float dy = ey - m_player.y;
            if (dx * dx + dy * dy < 5.0f * 5.0f) continue;        // not on player
            if (CircleBlockedByWalls(m_grid, ex, ey, kEnemyR)) continue;
            Enemy e;
            e.pos = { ex, ey };
            e.hp = kEnemyHpMax;
            m_enemies.push_back(e);
            ++placed;
        }
    }

    // Crude LOS — step kStep world units from a→b, fail if any sample lies in
    // a wall tile. Coarse but fine at melee/shoot distances.
    bool HasLOS(Vec2 a, Vec2 b) const {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) return true;
        const float step = 0.25f;
        const int n = int(len / step);
        for (int i = 1; i < n; ++i) {
            const float t = i * step / len;
            const int tx = int(a.x + dx * t);
            const int ty = int(a.y + dy * t);
            if (m_grid.At(tx, ty) == Tile::Wall) return false;
        }
        return true;
    }

    // ----- Rendering -----
    void DrawArenaFloor(Renderer2D& r) {
        // Outer rim — a slightly darker pad behind the playable grid so the
        // wall extrusion has a clean edge to sit on.
        const Vec2 topLeft = W2S({0, 0}, m_camOrigin);
        r.DrawQuad({topLeft.x - 8.0f, topLeft.y - 8.0f},
                   {kArenaW * kTileW + 16.0f, kArenaH * kTileH + 16.0f},
                   kArenaBorder);
        // Checker floor.
        for (int ty = 0; ty < kArenaH; ++ty) {
            for (int tx = 0; tx < kArenaW; ++tx) {
                if (m_grid.At(tx, ty) == Tile::Wall) continue;
                const Vec2 p = W2S({float(tx), float(ty)}, m_camOrigin);
                const Color c = ((tx + ty) & 1) ? kFloorA : kFloorB;
                r.DrawQuad(p, {kTileW, kTileH}, c);
            }
        }
    }

    // Walls + bullets + player + enemies, painter-sorted by feet world-Y.
    // Caller has fog OFF by the time we get here (it's only on for the floor
    // pass) — walls compute their distance tint per sub-quad and entities
    // tint via SampleShadowAt(feet). fogActive just tells us whether the
    // light disc is active, so we know whether to apply those tints at all.
    void DrawSorted(Renderer2D& r, bool fogActive) {
        struct Item {
            float sortY;
            int   kind;   // 0 wall, 1 enemy, 2 player, 3 bullet
            int   index;  // entity index where applicable
        };
        std::vector<Item> items;
        items.reserve(64 + m_enemies.size() + m_bullets.size());

        for (int ty = 0; ty < kArenaH; ++ty)
            for (int tx = 0; tx < kArenaW; ++tx)
                if (m_grid.At(tx, ty) == Tile::Wall)
                    items.push_back({float(ty) + 1.0f, 0, ty * kArenaW + tx});
        for (size_t i = 0; i < m_enemies.size(); ++i)
            items.push_back({m_enemies[i].pos.y, 1, int(i)});
        items.push_back({m_player.y, 2, 0});
        // Bullets are tagged with a small +epsilon so two bullets at the same
        // Y still draw in insertion order; the kind ordering tie-breaks them
        // above walls and players sharing the same Y.
        for (size_t i = 0; i < m_bullets.size(); ++i)
            items.push_back({m_bullets[i].pos.y + 0.001f, 3, int(i)});
        for (size_t i = 0; i < m_coins.size(); ++i)
            items.push_back({m_coins[i].pos.y - 0.001f, 4, int(i)});

        std::sort(items.begin(), items.end(),
                  [](const Item& a, const Item& b) {
                      if (a.sortY != b.sortY) return a.sortY < b.sortY;
                      return a.kind < b.kind;
                  });

        for (const Item& it : items) {
            switch (it.kind) {
                case 0: {
                    const int tx = it.index % kArenaW;
                    const int ty = it.index / kArenaW;
                    if (!fogActive) {
                        DrawWallFlat(r, tx, ty);
                        break;
                    }
                    // Closest point on the wall's footprint to the player —
                    // if even that is outside the light disc the whole wall
                    // is dark and we can use the 3-quad black fast path
                    // instead of sub-dividing.
                    const float cpx = std::max(float(tx),
                        std::min(m_player.x, float(tx + 1)));
                    const float cpy = std::max(float(ty),
                        std::min(m_player.y, float(ty + 1)));
                    const float ddx = m_player.x - cpx;
                    const float ddy = m_player.y - cpy;
                    if (ddx * ddx + ddy * ddy >= kLightRange * kLightRange) {
                        DrawWallBlack(r, tx, ty);
                    } else {
                        DrawWallShadowed(r, tx, ty);
                    }
                    break;
                }
                case 1: DrawEnemy(r, m_enemies[it.index]); break;
                case 2: DrawPlayer(r); break;
                case 3: DrawBullet(r, m_bullets[it.index]); break;
                case 4: DrawCoin(r, m_coins[it.index]); break;
            }
        }
    }

    // Per-vertex tint for walls — without it, every wall is tinted by its
    // single centre value and adjacent walls show a hard step in brightness
    // at their boundary. Each face is broken into a small grid and every
    // sub-quad samples its own world position, so a sub-quad on the seam
    // between two walls matches the equivalent sub-quad on the other side
    // and the chain reads as one continuous surface. Only runs when the
    // wall is in the lit area — the caller bails before we get here when
    // shadows are off, so the fast 3-quad path is preserved for that case.
    void DrawWallShadowed(Renderer2D& r, int tx, int ty) {
        constexpr int kSub = 6;
        const Vec2 p = W2S({float(tx), float(ty)}, m_camOrigin);
        const float sw      = kTileW       / float(kSub);
        const float shTop   = kTileH       / float(kSub);
        const float shSide  = kWallScreenH / float(kSub);

        const float r2 = kLightRange * kLightRange;
        auto TintAt = [this, r2](float wx, float wy) -> float {
            const float dx = wx - m_player.x;
            const float dy = wy - m_player.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= r2) return 0.0f;
            return 1.0f - d2 / r2;
        };
        auto mul = [](const Color& c, float t) {
            return Color(c.r * t, c.g * t, c.b * t, c.a);
        };

        // Side face — single horizontal strip (Y-axis doesn't change tint).
        // Sub-quads outside the disc collapse to black (mul by t=0); they
        // still draw so the wall reads as a black silhouette in the dark,
        // not as a hole in the scene.
        for (int sx = 0; sx < kSub; ++sx) {
            const float wx = float(tx) + (sx + 0.5f) / kSub;
            const float t  = TintAt(wx, float(ty) + 1.0f);
            r.DrawQuad({p.x + sx * sw, p.y}, {sw, shSide * kSub},
                       mul(kWallSide, t));
        }

        // Top face. World coords span (tx..tx+1, ty..ty+1) at z=1 — so
        // sub-divide in both axes. The seam at world Y=ty is exactly where
        // the north wall (if any) would have its top face's south edge,
        // and at ty+1 it meets THIS wall's own side face's south edge;
        // both meet at matching world coords, which is what makes the
        // brightness continuous across chains.
        for (int sy = 0; sy < kSub; ++sy) {
            for (int sx = 0; sx < kSub; ++sx) {
                const float wx = float(tx) + (sx + 0.5f) / kSub;
                const float wy = float(ty) + (sy + 0.5f) / kSub;
                const float t  = TintAt(wx, wy);
                r.DrawQuad({p.x + sx * sw,
                            p.y - kWallScreenH + sy * shTop},
                           {sw, shTop}, mul(kWallTop, t));
            }
        }

        // Edge highlight — same skip rule as before (only the northernmost
        // wall in a vertical chain draws it).
        if (m_grid.At(tx, ty - 1) != Tile::Wall) {
            for (int sx = 0; sx < kSub; ++sx) {
                const float wx = float(tx) + (sx + 0.5f) / kSub;
                const float t  = TintAt(wx, float(ty));
                r.DrawQuad({p.x + sx * sw, p.y - kWallScreenH},
                           {sw, 3.0f}, mul(kWallEdge, t));
            }
        }
    }

    // Fully-outside-the-disc fast path. Three flat black quads — no need to
    // sub-divide because every sub-quad would compute t=0 anyway.
    void DrawWallBlack(Renderer2D& r, int tx, int ty) {
        const Vec2 p = W2S({float(tx), float(ty)}, m_camOrigin);
        const Color black(0.0f, 0.0f, 0.0f, 1.0f);
        r.DrawQuad({p.x, p.y}, {kTileW, kWallScreenH}, black);
        r.DrawQuad({p.x, p.y - kWallScreenH}, {kTileW, kTileH}, black);
        // Edge highlight on a fully-dark wall reads as black-on-black, but
        // still skip when there's a wall above so the chain looks the same
        // in the dark as it does in light.
        if (m_grid.At(tx, ty - 1) != Tile::Wall) {
            r.DrawQuad({p.x, p.y - kWallScreenH}, {kTileW, 3.0f}, black);
        }
    }

    // Unshaded fast path used when SHADOWS is off — three flat quads, no
    // per-subquad work.
    void DrawWallFlat(Renderer2D& r, int tx, int ty) {
        const Vec2 p = W2S({float(tx), float(ty)}, m_camOrigin);
        r.DrawQuad({p.x, p.y}, {kTileW, kWallScreenH}, kWallSide);
        r.DrawQuad({p.x, p.y - kWallScreenH}, {kTileW, kTileH}, kWallTop);
        if (m_grid.At(tx, ty - 1) != Tile::Wall) {
            r.DrawQuad({p.x, p.y - kWallScreenH}, {kTileW, 3.0f}, kWallEdge);
        }
    }

    void DrawPlayer(Renderer2D& r) {
        const Vec2 feet = W2S(m_player, m_camOrigin);
        const float L = SampleShadowAt(m_player);
        auto tint = [L](const Color& c) {
            return Color(c.r * L, c.g * L, c.b * L, c.a);
        };
        DrawShadow(r, {feet.x, feet.y + 6.0f}, 18.0f);

        // Body: 26x18 squat torso so the upper half (head) reads above shoulders.
        const Color body = (m_playerHurt > 0.0f) ? kPlayerHurt : kPlayerBody;
        r.DrawQuad({feet.x - 14.0f, feet.y - 22.0f}, {28.0f, 18.0f}, tint(body));
        // Head.
        r.DrawQuad({feet.x - 9.0f, feet.y - 36.0f}, {18.0f, 16.0f}, tint(kPlayerHead));
        // Gun barrel: thin rotated quad pointing where the player aims, on
        // top of the body. Use the screen-space angle so the barrel matches
        // bullet travel under the Y compression.
        const float ang = WorldDirToScreenAngle(m_facing);
        const Vec2 gunMid{ feet.x + std::cos(ang) * 16.0f,
                           feet.y + std::sin(ang) * 16.0f - 18.0f };
        r.DrawQuad(gunMid, {18.0f, 5.0f}, ang, tint(kPlayerGun));
    }

    void DrawEnemy(Renderer2D& r, const Enemy& e) {
        const Vec2 feet = W2S(e.pos, m_camOrigin);
        const float L = SampleShadowAt(e.pos);
        auto tint = [L](const Color& c) {
            return Color(c.r * L, c.g * L, c.b * L, c.a);
        };
        DrawShadow(r, {feet.x, feet.y + 5.0f}, 16.0f);
        const Color body = (e.hurtFlash > 0.0f) ? Color(1.0f, 1.0f, 1.0f, 1.0f) : kEnemyBody;
        r.DrawQuad({feet.x - 13.0f, feet.y - 20.0f}, {26.0f, 17.0f}, tint(body));
        r.DrawQuad({feet.x - 8.0f,  feet.y - 32.0f}, {16.0f, 14.0f}, tint(kEnemyHead));
        const float ang = WorldDirToScreenAngle(e.aim);
        const Vec2 gunMid{ feet.x + std::cos(ang) * 14.0f,
                           feet.y + std::sin(ang) * 14.0f - 16.0f };
        r.DrawQuad(gunMid, {14.0f, 4.0f}, ang, tint(kEnemyGun));

        // HP pip strip above head — same tint, so a hidden enemy doesn't
        // give itself away with a floating red bar in the dark.
        const float dotW = 5.0f;
        const float dotH = 3.0f;
        const float gap  = 2.0f;
        const float stripW = kEnemyHpMax * dotW + (kEnemyHpMax - 1) * gap;
        for (int i = 0; i < kEnemyHpMax; ++i) {
            const Color c = (i < e.hp) ? Color(0.95f, 0.30f, 0.30f, 1.0f)
                                       : Color(0.20f, 0.10f, 0.10f, 0.7f);
            r.DrawQuad({feet.x - stripW * 0.5f + i * (dotW + gap),
                        feet.y - 42.0f},
                       {dotW, dotH}, tint(c));
        }
    }

    // In auto-aim mode, show the player what's happening:
    //   * a faint cone wedge along m_moveDir so they can see which way the
    //     auto-aim is searching;
    //   * a pulsing reticle on the current lock target so the next bullet's
    //     destination is obvious.
    // In mouse mode, this draws nothing — the cursor IS the feedback.
    void DrawAutoAimOverlay(Renderer2D& r) {
        if (m_ctx->aimMode != AimMode::Auto) return;
        const Vec2 pScreen = W2S(m_player, m_camOrigin);

        // Cone: a thin trapezoid along m_moveDir. We project two rays at the
        // cone half-angle into screen space and draw a low-alpha wedge by
        // stacking three rotated quads of decreasing thickness. The screen-
        // space direction has to bake in kYCompress or the wedge will not
        // line up with where bullets actually go visually.
        const float ang = WorldDirToScreenAngle(m_moveDir);
        for (int i = 0; i < 3; ++i) {
            const float reach = 60.0f + i * 60.0f;
            const float thick = 28.0f + i * 28.0f;
            const float alpha = 0.10f - i * 0.025f;
            const Vec2 mid{ pScreen.x + std::cos(ang) * (reach * 0.5f + 18.0f),
                            pScreen.y + std::sin(ang) * (reach * 0.5f + 18.0f) - 14.0f };
            r.DrawQuad(mid, {reach, thick}, ang,
                       Color(1.0f, 0.95f, 0.30f, alpha));
        }

        // Reticle on the lock target. Two crossed thin quads + four short
        // corner ticks. Pulse the alpha so the lock reads as "active".
        if (!m_autoLocked) return;
        const Vec2 t = W2S(m_autoTargetPos, m_camOrigin);
        const float pulse = 0.55f + 0.45f * std::sin(m_autoPulse * 9.0f);
        const Color rc(1.0f, 0.90f, 0.25f, pulse);
        const float feetOffset = -18.0f;   // sit above the target's head
        // Crosshair bars.
        r.DrawQuad({t.x - 14.0f, t.y + feetOffset - 1.0f}, {28.0f, 2.0f}, rc);
        r.DrawQuad({t.x - 1.0f,  t.y + feetOffset - 10.0f}, {2.0f, 20.0f}, rc);
        // Corner ticks (top-left, top-right, bottom-left, bottom-right of a
        // 22x22 box centred on the target's head).
        const float c = 11.0f;
        const float tk = 2.0f, ln = 5.0f;
        r.DrawQuad({t.x - c,        t.y + feetOffset - c    }, {ln, tk}, rc);
        r.DrawQuad({t.x - c,        t.y + feetOffset - c    }, {tk, ln}, rc);
        r.DrawQuad({t.x + c - ln,   t.y + feetOffset - c    }, {ln, tk}, rc);
        r.DrawQuad({t.x + c - tk,   t.y + feetOffset - c    }, {tk, ln}, rc);
        r.DrawQuad({t.x - c,        t.y + feetOffset + c - tk}, {ln, tk}, rc);
        r.DrawQuad({t.x - c,        t.y + feetOffset + c - ln}, {tk, ln}, rc);
        r.DrawQuad({t.x + c - ln,   t.y + feetOffset + c - tk}, {ln, tk}, rc);
        r.DrawQuad({t.x + c - tk,   t.y + feetOffset + c - ln}, {tk, ln}, rc);
    }

    // One rotated textured quad per particle — the sprite's per-pixel alpha
    // does all the shape work, so a single DrawQuad gives a smoother result
    // than the previous 4-quad cross at any rotation. spriteSize is sized
    // to ~2.2× the "main star width" so the soft rays fit inside the quad.
    // Floating "KILL N" text — anchored in world space so it scrolls with
    // the kill spot. Fades alpha by remaining life; horizontal-centred via
    // Font::Measure so any width of number lays flush above its anchor.
    void DrawPopups(Renderer2D& r) {
        if (m_popups.empty()) return;
        for (const Popup& p : m_popups) {
            if (!p.alive) continue;
            const Vec2 sp = W2S(p.pos, m_camOrigin);
            const float t = 1.0f - (p.age / p.life);
            const Vec2 ts = m_ctx->font->Measure(p.text, p.pxSize);
            const float bx = sp.x - ts.x * 0.5f;
            const float by = sp.y - 32.0f;
            const Color body(p.color.r, p.color.g, p.color.b, t);
            // Soft shadow for legibility on either floor or sparkle.
            r.DrawText(*m_ctx->font, p.text,
                       {bx + 2.0f, by + 2.0f},
                       Color(0.0f, 0.0f, 0.0f, t * 0.65f), p.pxSize);
            // Fake-bold for crit: paint the body 8 times around the centre
            // before the centre draw, thickening the glyph strokes by ~1.5px
            // without needing a separate bold font asset.
            if (p.bold) {
                constexpr float kOff = 1.5f;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        r.DrawText(*m_ctx->font, p.text,
                                   {bx + dx * kOff, by + dy * kOff},
                                   body, p.pxSize);
                    }
                }
            }
            r.DrawText(*m_ctx->font, p.text, {bx, by}, body, p.pxSize);
        }
    }

    void DrawParticles(Renderer2D& r) {
        if (!m_starTex || m_particles.empty()) return;
        for (const StarParticle& p : m_particles) {
            if (!p.alive) continue;
            const Vec2 sp = W2S(p.pos, m_camOrigin);
            const float t = 1.0f - (p.age / p.life);
            const float visible = p.size * (0.55f + 0.45f * t);
            const float spriteSize = visible * 2.2f;
            const Color tint(p.color.r, p.color.g, p.color.b, p.color.a * t);
            r.DrawQuad(sp, {spriteSize, spriteSize}, p.angle, m_starRegion, tint);
        }
    }

    // Coin: textured sprite with the 10-frame rotation cycle. The active
    // frame is picked from c.spin so the same kCoinSpinSpeed that drives
    // physics also drives the visible flip. Renders a floor shadow that
    // shrinks with z and the body lifted by z·kTileH.
    void DrawCoin(Renderer2D& r, const Coin& c) {
        const Vec2 feet = W2S(c.pos, m_camOrigin);
        const float L = SampleShadowAt(c.pos);

        // Floor shadow shrinks as the coin rises — a tiny depth cue.
        const float shadeShrink = 1.0f / (1.0f + c.z * 0.6f);
        const float shadeW = 10.0f * shadeShrink;
        const float shadeH = 3.0f  * shadeShrink;
        r.DrawQuad({feet.x - shadeW * 0.5f, feet.y - shadeH * 0.5f},
                   {shadeW, shadeH},
                   Color(0.0f, 0.0f, 0.0f, 0.35f * L));

        const float liftPx = c.z * kTileH;
        const Vec2 sp{ feet.x, feet.y - liftPx };

        // Spin → frame index. fmod into [0, 2π) without std::fmod's
        // negative-result surprise, then quantise to 10 buckets.
        constexpr float kTwoPi = 6.2831853f;
        const float wrapped = c.spin - std::floor(c.spin / kTwoPi) * kTwoPi;
        int frame = int(wrapped * float(kCoinFrameCount) / kTwoPi);
        if (frame < 0) frame = 0;
        if (frame >= kCoinFrameCount) frame = kCoinFrameCount - 1;

        if (m_coinTex) {
            const Color tint(L, L, L, 1.0f);
            r.DrawQuad({sp.x - kCoinSpriteW * 0.5f, sp.y - kCoinSpriteH * 0.5f},
                       {kCoinSpriteW, kCoinSpriteH},
                       m_coinFrames[frame], tint);
        }
    }

    void DrawBullet(Renderer2D& r, const Bullet& b) {
        const Vec2 sp = W2S(b.pos, m_camOrigin);
        const float L = SampleShadowAt(b.pos);
        const Color base = b.fromPlayer ? kPBullet : kEBullet;
        const Color c(base.r * L, base.g * L, base.b * L, base.a);
        // Tiny trail — a faded copy 4-6px behind the bullet's screen velocity.
        const float ang = std::atan2(b.vel.y * kYCompress, b.vel.x);
        const Vec2 tail{ sp.x - std::cos(ang) * 8.0f,
                         sp.y - std::sin(ang) * 8.0f };
        r.DrawQuad(tail, {6.0f, 4.0f}, ang,
                   Color(c.r, c.g, c.b, 0.55f));
        // Bullet body — small quad rotated to face direction of travel.
        r.DrawQuad(sp, {10.0f, 6.0f}, ang, c);
    }

    // Mid-run pause overlay. A light dim so the player can still see the
    // frozen state (positions of enemies, where their next shot would go),
    // plus a "PAUSED" label and a blinking resume / quit hint.
    void DrawPauseOverlay(Renderer2D& r) {
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH},
                   Color(0.0f, 0.0f, 0.0f, 0.35f));
        DrawCenteredText(r, *m_ctx->font, "PAUSED",
                         kScreenH * 0.32f, 84.0f,
                         Color(1.0f, 1.0f, 1.0f, 1.0f));
        const float blink = 0.5f + 0.5f * std::sin(m_pausePulse * 3.0f);
        DrawCenteredText(r, *m_ctx->font, "Esc resume   Q quit to title",
                         kScreenH * 0.55f, 26.0f,
                         Color(1.0f, 1.0f, 1.0f, blink));
    }

    // Game-over overlay drawn on top of the frozen play screen. Dim layer
    // first, then big red GAME OVER, the run summary, and a blinking
    // restart prompt. Lives on top of the HUD so the player can still see
    // their wave/score in the bar above.
    void DrawDeathOverlay(Renderer2D& r) {
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH},
                   Color(0.0f, 0.0f, 0.0f, 0.55f));
        DrawCenteredText(r, *m_ctx->font, "GAME OVER",
                         kScreenH * 0.30f, 96.0f,
                         Color(0.95f, 0.30f, 0.30f, 1.0f));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "WAVE %d   SCORE %d",
                      m_ctx->wave, m_ctx->score);
        DrawCenteredText(r, *m_ctx->font, buf,
                         kScreenH * 0.45f, 34.0f,
                         Color(1.0f, 1.0f, 1.0f, 1.0f));
        const float blink = 0.5f + 0.5f * std::sin(m_deathPulse * 3.0f);
        DrawCenteredText(r, *m_ctx->font, "Enter retry   Esc title",
                         kScreenH * 0.60f, 28.0f,
                         Color(1.0f, 1.0f, 1.0f, blink));
    }

    void DrawHud(Renderer2D& r) {
        // Top bar.
        r.DrawQuad({0, 0}, {kScreenW, 48.0f}, kHudBg);
        // HP bar.
        const float hpFrac = std::max(0.0f, float(m_ctx->playerHp) / float(kPlayerHpMax));
        r.DrawQuad({16, 12}, {240.0f, 24.0f}, kHpBarBg);
        r.DrawQuad({18, 14}, {236.0f * hpFrac, 20.0f}, kHpBarFill);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "HP %d / %d", m_ctx->playerHp, kPlayerHpMax);
        r.DrawText(*m_ctx->font, buf, {270.0f, 14.0f},
                   Color(1.0f, 1.0f, 1.0f, 1.0f), 22.0f);

        std::snprintf(buf, sizeof(buf), "WAVE %d", std::max(1, m_ctx->wave));
        const Vec2 wSize = m_ctx->font->Measure(buf, 26.0f);
        r.DrawText(*m_ctx->font, buf,
                   {(kScreenW - wSize.x) * 0.5f, 12.0f},
                   Color(1.0f, 0.95f, 0.40f, 1.0f), 26.0f);

        std::snprintf(buf, sizeof(buf), "SCORE %d", m_ctx->score);
        const Vec2 sSize = m_ctx->font->Measure(buf, 24.0f);
        r.DrawText(*m_ctx->font, buf,
                   {kScreenW - sSize.x - 20.0f, 14.0f},
                   Color(1.0f, 1.0f, 1.0f, 1.0f), 24.0f);

        // Aim mode pill — sits just under the top bar so the player always
        // knows which mode is active. Flashes briefly after a T toggle.
        const bool autoOn = (m_ctx->aimMode == AimMode::Auto);
        const Color modeBase = autoOn ? Color(1.0f, 0.85f, 0.25f, 1.0f)
                                      : Color(0.55f, 0.85f, 1.00f, 1.0f);
        const float fade = (m_modeFlash > 0.0f) ? (m_modeFlash / 0.8f) : 0.0f;
        const Color modeColor(
            modeBase.r + (1.0f - modeBase.r) * fade,
            modeBase.g + (1.0f - modeBase.g) * fade,
            modeBase.b + (1.0f - modeBase.b) * fade,
            1.0f);
        const char* modeText = autoOn ? "AIM: AUTO   [T] mouse"
                                      : "AIM: MOUSE  [T] auto";
        r.DrawQuad({16.0f, 56.0f},
                   {(autoOn ? 245.0f : 250.0f), 26.0f},
                   Color(0.0f, 0.0f, 0.0f, 0.50f));
        r.DrawText(*m_ctx->font, modeText, {26.0f, 60.0f}, modeColor, 20.0f);

        // Shadow toggle pill, same shape as the AIM pill but one row down.
        const bool shadOn = m_ctx->shadowsOn;
        const Color shadBase = shadOn ? Color(0.55f, 0.85f, 0.45f, 1.0f)
                                      : Color(0.65f, 0.65f, 0.65f, 1.0f);
        const float sfade = (m_shadowFlash > 0.0f) ? (m_shadowFlash / 0.8f) : 0.0f;
        const Color shadColor(
            shadBase.r + (1.0f - shadBase.r) * sfade,
            shadBase.g + (1.0f - shadBase.g) * sfade,
            shadBase.b + (1.0f - shadBase.b) * sfade,
            1.0f);
        const char* shadText = shadOn ? "SHADOWS: ON   [H] off"
                                      : "SHADOWS: OFF  [H] on";
        r.DrawQuad({16.0f, 88.0f}, {230.0f, 26.0f},
                   Color(0.0f, 0.0f, 0.0f, 0.50f));
        r.DrawText(*m_ctx->font, shadText, {26.0f, 92.0f}, shadColor, 20.0f);

        // Wave preamble — center prompt when between waves.
        if (!m_waveActive && m_enemies.empty()) {
            const float t = m_waveTimer / kBetweenWaves;
            const float fade = Clamp01(1.0f - std::fabs(t - 0.5f) * 2.0f);
            char prompt[64];
            std::snprintf(prompt, sizeof(prompt), "WAVE %d INCOMING",
                          std::max(1, m_ctx->wave + 1));
            DrawCenteredText(r, *m_ctx->font, prompt,
                             kScreenH * 0.40f, 48.0f,
                             Color(1.0f, 0.95f, 0.30f, 0.30f + 0.70f * fade));
        }
    }

    static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    // ----- Data -----
    GameContext* m_ctx = nullptr;

    Grid    m_grid;
    Vec2    m_playerSpawn{};
    Vec2    m_player{};
    Vec2    m_facing{1.0f, 0.0f};
    // Last non-zero WASD direction. Persists when the player stops so the
    // auto-aim cone keeps pointing where they were heading. Initialised right
    // so the cone has *some* direction on the very first frame.
    Vec2    m_moveDir{1.0f, 0.0f};
    Vec2    m_playerKnockback{0.0f, 0.0f};   // bullet-hit impulse, decays each frame
    Vec2    m_camOrigin{0.0f, 0.0f};
    float   m_playerHurt = 0.0f;
    float   m_fireCd     = 0.0f;
    // Auto-aim solve from this frame's UpdatePlayer. Cached so DrawHud and
    // the reticle render can use it without re-scanning the enemy list.
    bool    m_autoLocked = false;
    Vec2    m_autoTargetPos{0.0f, 0.0f};
    float   m_autoPulse = 0.0f;     // reticle blink
    float   m_modeFlash = 0.0f;     // brief HUD highlight after a mode toggle
    // Ray-cast shadow texture (R = light multiplier in [0, 1]). Rebuilt by
    // UpdateShadows() any time the player moves a non-trivial amount or the
    // toggle flips on. Drives Renderer2D::SetFog during the world pass.
    std::unique_ptr<RHITexture> m_shadowTex;
    std::vector<uint8_t>        m_shadowPixels;   // tightly packed RGBA8
    // Procedural sparkle sprite used by every death star. Generated once on
    // scene entry — a smooth 128×128 RGBA8 with bright core + 4 main rays
    // and 4 shorter diagonal rays. Drawn rotated with linear filtering, so
    // it stays sharp at any orientation instead of the chunky look the
    // axis-aligned rotated rectangles produced.
    std::unique_ptr<RHITexture> m_starTex;
    TextureRegion               m_starRegion;
    // Coin sprite sheet (5×2 frames). Loaded once per scene from
    // assets/coin.png; m_coinFrames[i] points at frame i in cell-row order.
    std::unique_ptr<RHITexture> m_coinTex;
    TextureRegion               m_coinFrames[kCoinFrameCount];
    int     m_shadowW = 0, m_shadowH = 0;
    Vec2    m_lastShadowSrc{1e9f, 1e9f};          // forces first-frame rebuild
    float   m_shadowFlash = 0.0f;                 // HUD flash after H toggle
    std::vector<Bullet>        m_bullets;
    std::vector<Enemy>         m_enemies;
    std::vector<StarParticle>  m_particles;
    std::vector<Popup>         m_popups;
    std::vector<Coin>          m_coins;
    bool    m_waveActive = false;
    float   m_waveTimer  = 0.0f;
    // True once the player's HP hits zero. We stay in this scene instead of
    // pushing a GameOver overlay: gameplay updates pause, particles keep
    // animating, and a blinking GAME OVER text waits for Enter/Esc.
    bool    m_dead       = false;
    float   m_deathPulse = 0.0f;
    // True while the player has hit Esc mid-run. Freezes all gameplay
    // updates and dims the screen with an overlay.
    bool    m_paused     = false;
    float   m_pausePulse = 0.0f;

    std::mt19937 m_rngArena{0};
    std::mt19937 m_rng{std::random_device{}()};
};

} // namespace

// ---------------------------------------------------------------------------
// Application.
// ---------------------------------------------------------------------------
class BrawlApp : public Application {
public:
    BrawlApp() : Application(MakeConfig()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;
        m_font = LoadMSDFFont(GetDevice(), assets + "/msdf-inter.json");
        if (!m_font.Valid()) {
            NARAGAME_ERROR("brawl: failed to load MSDF font at %s/msdf-inter.json",
                           assets.c_str());
        }

        m_ctx.renderer = &m_renderer;
        m_ctx.font     = &m_font;
        m_ctx.device   = &GetDevice();
        m_ctx.makeArena    = [this] { return std::make_unique<ArenaScene>(&m_ctx); };
        m_ctx.makeTitle    = [this] { return std::make_unique<TitleScene>(&m_ctx); };
        m_scenes.Change(m_ctx.makeTitle());
    }

    void OnUpdate(float dt) override { m_scenes.Update(dt); }
    void OnRender() override         { m_scenes.Render(); }
    void OnStop() override           { m_scenes.Clear(); m_renderer.Shutdown(); }

private:
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title  = "Brawl Shooter";
        c.width  = uint32_t(kScreenW);
        c.height = uint32_t(kScreenH);
        c.vsync  = true;
        return c;
    }

    Renderer2D   m_renderer;
    MSDFFont     m_font;
    SceneManager m_scenes;
    GameContext  m_ctx;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new BrawlApp();
}
