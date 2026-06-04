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
constexpr int   kPlayerHpMax = 100;
constexpr float kPlayerHurtIFrame = 0.55f;
constexpr float kPlayerFireCd     = 0.18f;
constexpr float kPlayerRegenRate  = 1.00f;  // HP/sec while standing still
// Idle auto-turn: when the player is standing still in Auto mode and any
// enemy is in sight, m_moveDir rotates toward that enemy at this angular
// speed. Slow enough to look deliberate, fast enough to keep up with a
// moving target.
constexpr float kPlayerTurnSpeed  = 7.0f;   // rad/s

// Bullets.
constexpr float kBulletR        = 0.13f;
constexpr float kPlayerBulletV  = 14.5f;
constexpr float kEnemyBulletV   = 8.0f;
constexpr float kBulletLife     = 0.85f;
constexpr int   kPlayerBulletDmg = 10;
constexpr int   kEnemyBulletDmg  = 10;

// Enemies.
constexpr float kEnemyR             = 0.34f;
constexpr float kEnemySpeed         = 2.6f;
constexpr int   kEnemyHpMax         = 50;
constexpr int   kEnemyHpPerWave     = 5;    // added on top of base HP each wave
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
constexpr float kKnockbackDecay   = 18.0f;  // 1/s; smaller = longer slide

// Sparkle aerodynamic drag — shared by every StarParticle (currently only
// the wall-hit white sparks, but the system is generic).
constexpr float kStarDrag  = 3.0f;          // 1/s velocity decay

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
constexpr float kCoinSpinSpeed   = 14.0f;  // rad/s about the visible "axis"
constexpr float kCoinSpinSettled = 6.0f;   // rad/s once it has come to rest
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

// Coin lifetime: 10 s of solid idling, then 5 s blinking, then gone. Counted
// from spawn, so a coin spends roughly the first second still bouncing —
// close enough to the user-facing "leave it for 10 s" intent.
constexpr float kCoinIdleLife  = 10.0f;
constexpr float kCoinBlinkLife = 5.0f;
constexpr float kCoinFullLife  = kCoinIdleLife + kCoinBlinkLife;
constexpr float kCoinBlinkRate = 4.0f;     // Hz; square-wave on/off

// Item drops — 15% of kills spit out one item instead of a coin burst.
// Adding a new item: extend ItemKind, add a row to ItemDefOf, add a case
// to ArenaScene::ApplyItemPickup. Box geometry + life / physics / pickup
// detection all flow through ItemKind dispatch.
constexpr float kItemDropChance  = 0.20f;
constexpr float kItemBoxPx       = 32.0f;  // box side on screen
constexpr float kItemBoxRadius   = 4.0f;   // rounded-corner radius
constexpr float kItemBorderPx    = 2.0f;   // gray frame thickness
constexpr float kItemPowerMult   = 1.20f;  // multiplicative on m_playerDmgMult (each = +20%)
constexpr float kItemSpeedMult   = 1.10f;  // multiplicative on m_playerRateMult (each = +10%)
constexpr int   kMultiMaxShots   = 8;      // hard cap on m_playerShotCount
constexpr float kMultiSpreadDeg  = 5.0f;   // degrees between adjacent bullets
constexpr float kItemHealFrac    = 0.20f;  // fraction of kPlayerHpMax restored

enum class ItemKind : uint8_t { Power, Speed, Heal, Multi };

// The visible shape of every item is identical — a gray-bordered black
// rounded square — so the per-kind data is just the letter and the colour
// of that letter (which also drives the pickup popup tint, so the player
// keeps the same colour cue from box to popup).
struct ItemDef {
    char  letter;
    Color color;
};

inline ItemDef ItemDefOf(ItemKind k) {
    switch (k) {
        case ItemKind::Power: return { 'P', Color(1.00f, 0.45f, 0.95f, 1.0f) };
        case ItemKind::Speed: return { 'S', Color(0.40f, 0.85f, 1.00f, 1.0f) };
        case ItemKind::Heal:  return { 'H', Color(0.55f, 1.00f, 0.45f, 1.0f) };
        case ItemKind::Multi: return { 'M', Color(1.00f, 0.65f, 0.20f, 1.0f) };
    }
    return { '?', Color::White() };
}

inline ItemKind RandomItemKind(std::mt19937& rng) {
    std::uniform_int_distribution<int> d(0, 3);
    return ItemKind(d(rng));
}

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
    float age     = 0.0f;   // seconds since spawn; drives expiry + blink
    bool  settled = false;
    bool  alive   = true;
};

// Same physics shape as a Coin (gravity, bounce, wall slide, age + blink).
// kind drives the visual + pickup effect.
struct Item {
    ItemKind kind = ItemKind::Power;
    Vec2  pos{};
    Vec2  vel{};
    float z       = 0.0f;
    float vz      = 0.0f;
    float age     = 0.0f;
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

// One "star" of a sparkle burst. Currently driven only by EmitWallSparks
// (white bullet-vs-wall sparks); the per-particle life / size / colour
// fields keep the system flexible for future emitters.
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
    int  hp    = kEnemyHpMax;
    int  hpMax = kEnemyHpMax;     // per-enemy max so HP bar scales when waves toughen
    float fireCd = 0.0f;
    float hurtFlash = 0.0f;
    bool alive = true;
    // Wander target re-rolled when reached or after wanderCd elapses, so
    // enemies strafe around instead of marching in a straight line.
    Vec2 wander{0.0f, 0.0f};
    float wanderCd = 0.0f;
    Vec2 knockback{0.0f, 0.0f};   // residual hit impulse, decays in UpdateEnemies
    float animTime = 0.0f;         // walk-cycle accumulator (reset when idle)
};

// ---------------------------------------------------------------------------
// Aim modes.
// Only enemies in the player's MOVEMENT direction (within a 90° forward
// cone, in line-of-sight, within kAutoAimRange) are lockable; closest
// such enemy is the target. Fire is automatic — a bullet leaves the gun
// every cooldown tick whenever there's a lock. No target → no shot.
// ---------------------------------------------------------------------------

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
        DrawCenteredText(r, *m_ctx->font, "WASD move   Auto-fire on lock",
                         540.0f, 22.0f, Color(0.75f, 0.85f, 0.85f, 1.0f));
        DrawCenteredText(r, *m_ctx->font, "R rebuild   Esc back",
                         580.0f, 22.0f, Color(0.65f, 0.75f, 0.75f, 1.0f));
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
        m_items.clear();
        m_playerDmgMult   = 1.0f;
        m_playerRateMult  = 1.0f;
        m_playerShotCount = 1;
        m_waveTimer  = 0.0f;
        m_waveActive = false;
        m_itemThisWave = false;
        m_ctx->wave  = 0;
        m_facing     = {1.0f, 0.0f};
        m_moveDir    = {1.0f, 0.0f};
        m_hpRegenAccum = 0.0f;
        m_autoLocked = false;
        m_autoPulse  = 0.0f;
        m_shadowFlash = 0.0f;
        m_lastShadowSrc = {1e9f, 1e9f};
        m_dead = false;
        m_deathPulse = 0.0f;
        m_lightRange = kLightRange;
        m_paused = false;
        m_pausePulse = 0.0f;
        EnsureShadowTexture();
        EnsureStarTexture();
        EnsureCoinSprite();
        EnsurePieTexture();
        EnsurePlayerSprite();
        EnsureEnemySprite();
        m_animTime = 0.0f;
    }

    void EnsureEnemySprite() {
        if (m_enemyTex || !m_ctx->device) return;
        const std::string path =
            std::string(NARAGAME_ASSETS_DIR) + "/enemy.png";
        m_enemyTex = LoadTexture(*m_ctx->device, path, TextureFilter::Point);
        if (!m_enemyTex) {
            NARAGAME_WARN("brawl: enemy sprite '%s' missing", path.c_str());
            return;
        }
        constexpr float kFW = 48.0f;
        constexpr float kFH = 56.0f;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                m_enemyFrames[row][col] = TextureRegion::FromPixels(
                    m_enemyTex.get(), col * kFW, row * kFH, kFW, kFH);
            }
        }
    }

    // Atlas layout: 4 rows × 8 cols, rows = (N, NE, SE, S), cols = walk
    // frame index, each cell 60×80. Cached as TextureRegions for direct
    // DrawQuad use.
    void EnsurePlayerSprite() {
        if (m_playerTex || !m_ctx->device) return;
        const std::string path =
            std::string(NARAGAME_ASSETS_DIR) + "/player.png";
        m_playerTex = LoadTexture(*m_ctx->device, path, TextureFilter::Point);
        if (!m_playerTex) {
            NARAGAME_WARN("brawl: player sprite '%s' missing", path.c_str());
            return;
        }
        constexpr float kFW = 60.0f;
        constexpr float kFH = 80.0f;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 8; ++col) {
                m_playerFrames[row][col] = TextureRegion::FromPixels(
                    m_playerTex.get(), col * kFW, row * kFH, kFW, kFH);
            }
        }
    }

    // Bake the auto-aim cone sprite. Texture is square; the wedge points
    // along +x in texture space, so DrawQuad's rotation parameter directly
    // aligns with WorldDirToScreenAngle(m_moveDir) at render time. Per-pixel
    // alpha encodes a radial gradient (bright apex → faded outer arc) with
    // a soft angular smoothstep at the wedge edges to anti-alias the cone.
    void EnsurePieTexture() {
        if (m_pieTex || !m_ctx->device) return;
        constexpr int kN = 256;
        std::vector<uint8_t> px(size_t(kN) * kN * 4);
        const float halfA = std::acos(kAutoAimConeCos);
        const float edgeBand = 0.10f;   // rad; smoothstep half-width at edges
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                const float fx = (float(x) + 0.5f) / float(kN) * 2.0f - 1.0f;
                const float fy = (float(y) + 0.5f) / float(kN) * 2.0f - 1.0f;
                const float r2 = fx * fx + fy * fy;
                float alpha = 0.0f;
                if (r2 < 1.0f) {
                    const float r       = std::sqrt(r2);
                    const float theta   = std::atan2(fy, fx);
                    const float absTh   = std::fabs(theta);
                    if (absTh < halfA) {
                        // Radial: 1.0 at apex → 0.30 at rim, then linear to 0
                        // in the last ~5% so the outer edge is anti-aliased.
                        float radial = 1.0f - r * 0.70f;
                        if (r > 0.95f) radial *= (1.0f - r) / 0.05f;
                        // Angular smoothstep over edgeBand at each side.
                        const float edge   = halfA - absTh;
                        const float t      = std::min(1.0f, edge / edgeBand);
                        const float angle  = t * t * (3.0f - 2.0f * t);
                        alpha = radial * angle;
                    }
                }
                const uint8_t v = uint8_t(alpha * 255.0f + 0.5f);
                const size_t idx = (size_t(y) * kN + x) * 4;
                px[idx + 0] = 255;
                px[idx + 1] = 255;
                px[idx + 2] = 255;
                px[idx + 3] = v;
            }
        }
        TextureDesc td;
        td.width  = kN;
        td.height = kN;
        td.format = TextureFormat::RGBA8;
        td.filter = TextureFilter::Linear;
        td.pixels = px.data();
        m_pieTex = m_ctx->device->CreateTexture(td);
        if (m_pieTex)
            m_pieRegion = TextureRegion::FromPixels(
                m_pieTex.get(), 0.0f, 0.0f, float(kN), float(kN));
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

    // 20% chance: one random item box; otherwise the regular coin shower.
    // Either way the kill spawn is consolidated here.
    void EmitDrop(Vec2 at) {
        std::uniform_real_distribution<float> roll(0.0f, 1.0f);
        if (roll(m_rng) < kItemDropChance) {
            EmitItem(at, RandomItemKind(m_rng));
            m_itemThisWave = true;
        } else {
            EmitCoins(at);
        }
    }

    // Spawn one item box at `at` with the same fountain physics a coin uses.
    void EmitItem(Vec2 at, ItemKind kind) {
        std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> vzDist(kCoinInitVzMin, kCoinInitVzMax);
        std::uniform_real_distribution<float> vxyDist(kCoinInitVxyMin, kCoinInitVxyMax);
        const float a   = angle(m_rng);
        const float vxy = vxyDist(m_rng);
        Item it;
        it.kind = kind;
        it.pos  = at;
        it.vel  = { std::cos(a) * vxy, std::sin(a) * vxy };
        it.vz   = vzDist(m_rng);
        m_items.push_back(it);
    }

    // Pickup dispatch. Adding a new item: add a case here. The popup is
    // emitted at the pickup point so the player sees what they just grabbed.
    void ApplyItemPickup(const Item& it) {
        const ItemDef def = ItemDefOf(it.kind);
        Popup p;
        p.pos    = it.pos;
        p.color  = def.color;
        p.pxSize = kPopupPx * 1.1f;
        p.life   = kPopupLife;
        p.bold   = true;
        switch (it.kind) {
            case ItemKind::Power:
                m_playerDmgMult *= kItemPowerMult;
                std::snprintf(p.text, sizeof(p.text), "POWER +20%%");
                break;
            case ItemKind::Speed:
                m_playerRateMult *= kItemSpeedMult;
                std::snprintf(p.text, sizeof(p.text), "FIRE +10%%");
                break;
            case ItemKind::Heal: {
                const int target  = int(kPlayerHpMax * kItemHealFrac) + m_ctx->playerHp;
                const int clamped = std::min(int(kPlayerHpMax), target);
                const int actual  = clamped - m_ctx->playerHp;   // never above max
                m_ctx->playerHp   = clamped;
                std::snprintf(p.text, sizeof(p.text), "+%d", actual);
                break;
            }
            case ItemKind::Multi:
                if (m_playerShotCount < kMultiMaxShots) m_playerShotCount += 1;
                std::snprintf(p.text, sizeof(p.text), "MULTI x%d", m_playerShotCount);
                break;
        }
        m_popups.push_back(p);
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
            m_pauseMenuIdx = 0;   // always start on Resume
            m_pauseLastMouse = Input::MousePosition();
        }

        m_shadowFlash = std::max(0.0f, m_shadowFlash - dt);

        // Once dead we freeze gameplay updates but keep particles running
        // and listen for restart input.
        if (m_dead) {
            m_deathPulse += dt;
            UpdateParticles(dt);
            UpdatePopups(dt);
            UpdateCoins(dt, /*pickupEnabled=*/false);
            UpdateItems(dt, /*pickupEnabled=*/false);
            // Vision tunnel-closes from kLightRange → 0 over ~2.5 s. Force
            // a shadow rebuild every frame while it's shrinking so the
            // texture reflects the new radius — the dirty check otherwise
            // skips because the player position doesn't change.
            constexpr float kDeathFadeRate = 3.6f;  // tiles/s
            if (m_lightRange > 0.0f) {
                m_lightRange = std::max(0.0f, m_lightRange - kDeathFadeRate * dt);
                m_lastShadowSrc = {1e9f, 1e9f};
                if (m_ctx->shadowsOn) UpdateShadows();
            }
            if (Input::IsKeyPressed(Key::Enter) || Input::IsKeyPressed(Key::Space)) {
                m_ctx->playerHp = kPlayerHpMax;
                m_ctx->score    = 0;
                m_ctx->wave     = 0;
                m_ctx->kills    = 0;
                Manager().Change(m_ctx->makeArena());
            }
            return;
        }

        // Paused: gameplay frozen. The pause overlay shows a 3-item menu —
        // Resume / Surrender / Exit — driven by arrow keys + Enter or by
        // mouse hover + click. Esc still toggles the pause, so the player
        // can back out without going through the menu.
        if (m_paused) {
            m_pausePulse += dt;
            if (Input::IsKeyPressed(Key::Up))
                m_pauseMenuIdx = (m_pauseMenuIdx - 1 + kPauseMenuCount) % kPauseMenuCount;
            if (Input::IsKeyPressed(Key::Down))
                m_pauseMenuIdx = (m_pauseMenuIdx + 1) % kPauseMenuCount;
            // Hover only takes effect on the frame the mouse actually moves —
            // otherwise a cursor parked on top of the menu would overwrite
            // every keyboard-driven selection on the very next frame.
            const Vec2 mp = Input::MousePosition();
            const bool mouseMoved = (mp.x != m_pauseLastMouse.x) ||
                                    (mp.y != m_pauseLastMouse.y);
            m_pauseLastMouse = mp;
            const int hovered = PauseMenuItemAtMouse(mp);
            if (mouseMoved && hovered >= 0) m_pauseMenuIdx = hovered;
            const bool clicked = Input::IsMousePressed(MouseButton::Left) && hovered >= 0;
            const bool entered = Input::IsKeyPressed(Key::Enter) ||
                                 Input::IsKeyPressed(Key::Space);
            if (clicked || entered) ActivatePauseMenu(m_pauseMenuIdx);
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
        // Debug-only: F8 toggles the shadow pass. Not advertised in the
        // title screen instructions — leaving it on is the intended look,
        // but having a hidden disable keeps the rendering path testable
        // without recompiling.
        if (Input::IsKeyPressed(Key::F8)) {
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
        UpdateItems(dt, /*pickupEnabled=*/true);
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
        const float r2 = m_lightRange * m_lightRange;
        if (r2 <= 0.0f || d2 >= r2) return 0.0f;
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

        const float R     = m_lightRange;
        const bool  hasLight = R > 0.001f;
        const float invR2 = hasLight ? 1.0f / (R * R) : 0.0f;
        const float inv   = 1.0f / float(kShadowSubdiv);
        for (int sy = 0; sy < m_shadowH; ++sy) {
            for (int sx = 0; sx < m_shadowW; ++sx) {
                const float wx = (sx + 0.5f) * inv;
                const float wy = (sy + 0.5f) * inv;
                float light;
                if (m_grid.At(int(wx), int(wy)) == Tile::Wall) {
                    light = 1.0f;
                } else if (!hasLight) {
                    light = kAmbient;
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
    // Closest live enemy that's within the light disc and in LOS. Used by
    // the idle auto-turn: when standing still, the body slowly rotates to
    // face whichever enemy is closest "in sight", independent of the cone.
    const Enemy* FindNearestVisibleEnemy() const {
        const Enemy* best = nullptr;
        float bestD2 = m_lightRange * m_lightRange;
        for (const Enemy& e : m_enemies) {
            if (!e.alive) continue;
            const float dx = e.pos.x - m_player.x;
            const float dy = e.pos.y - m_player.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= bestD2) continue;
            if (!HasLOS(m_player, e.pos)) continue;
            bestD2 = d2;
            best = &e;
        }
        return best;
    }

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

        // Movement.
        Vec2 input{0.0f, 0.0f};
        if (Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::Left))  input.x -= 1.0f;
        if (Input::IsKeyDown(Key::D) || Input::IsKeyDown(Key::Right)) input.x += 1.0f;
        if (Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::Up))    input.y -= 1.0f;
        if (Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::Down))  input.y += 1.0f;
        const float ilen = std::sqrt(input.x * input.x + input.y * input.y);
        if (ilen > 0.001f) {
            input = { input.x / ilen, input.y / ilen };
            // Latch the latest movement direction. The auto-aim cone keeps
            // pointing this way after the player stops, so a quick tap-stop-
            // shoot still fires "ahead" of the run.
            m_moveDir = input;
            m_animTime += dt;
        } else {
            m_animTime = 0.0f;
            // Stationary. Two idle behaviours:
            //   1) Regen HP at kPlayerRegenRate per second (banked fractional).
            //   2) Slowly rotate m_moveDir toward the nearest visible enemy
            //      so the auto-aim cone follows them without the player
            //      having to nudge WASD.
            if (m_ctx->playerHp < int(kPlayerHpMax)) {
                m_hpRegenAccum += kPlayerRegenRate * dt;
                while (m_hpRegenAccum >= 1.0f) {
                    m_hpRegenAccum -= 1.0f;
                    m_ctx->playerHp = std::min(int(kPlayerHpMax),
                                               m_ctx->playerHp + 1);
                }
            } else {
                m_hpRegenAccum = 0.0f;
            }
            if (const Enemy* tgt = FindNearestVisibleEnemy()) {
                const float dx  = tgt->pos.x - m_player.x;
                const float dy  = tgt->pos.y - m_player.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.001f) {
                    constexpr float kTwoPi = 6.2831853f;
                    const float desiredA = std::atan2(dy, dx);
                    const float currentA = std::atan2(m_moveDir.y, m_moveDir.x);
                    float delta = desiredA - currentA;
                    // Wrap to [-π, π] so we always turn the short way.
                    while (delta >  3.14159265f) delta -= kTwoPi;
                    while (delta < -3.14159265f) delta += kTwoPi;
                    const float maxStep = kPlayerTurnSpeed * dt;
                    if (delta >  maxStep) delta =  maxStep;
                    if (delta < -maxStep) delta = -maxStep;
                    const float newA = currentA + delta;
                    m_moveDir = { std::cos(newA), std::sin(newA) };
                }
            }
        }

        const float step = kPlayerSpeed * dt;
        const float nx = m_player.x + input.x * step;
        const float ny = m_player.y + input.y * step;
        // Axis-separated collision so the player can slide along walls.
        if (!CircleBlockedByWalls(m_grid, nx, m_player.y, kPlayerR)) m_player.x = nx;
        if (!CircleBlockedByWalls(m_grid, m_player.x, ny, kPlayerR)) m_player.y = ny;

        // Aim. Auto-aim picks the locked target's direction; with no lock,
        // the gun points along m_moveDir so the sprite still reads.
        m_autoLocked = false;
        if (const Enemy* tgt = FindAutoTarget()) {
            const float dx = tgt->pos.x - m_player.x;
            const float dy = tgt->pos.y - m_player.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            m_facing = { dx / len, dy / len };
            m_autoLocked    = true;
            m_autoTargetPos = tgt->pos;
        } else {
            m_facing = m_moveDir;
        }

        // Shoot: no input required, the gun auto-fires whenever there's a
        // lock and the cooldown has elapsed.
        if (m_autoLocked && m_fireCd <= 0.0f) {
            m_fireCd = kPlayerFireCd / m_playerRateMult;
            // Multi-shot: m_playerShotCount bullets fanned symmetrically
            // around m_facing with kMultiSpreadDeg between adjacent shots.
            // For N shots the angle offset of shot i is
            //   (i − (N−1)/2) · kMultiSpreadDeg   degrees.
            const int   N        = std::max(1, m_playerShotCount);
            const float spreadRd = kMultiSpreadDeg * 3.14159265f / 180.0f;
            const float centre   = float(N - 1) * 0.5f;
            for (int i = 0; i < N; ++i) {
                const float a  = (float(i) - centre) * spreadRd;
                const float ca = std::cos(a), sa = std::sin(a);
                const float dx = m_facing.x * ca - m_facing.y * sa;
                const float dy = m_facing.x * sa + m_facing.y * ca;
                Bullet b;
                // Spawn at gun tip rather than the player's center so the bullet
                // doesn't appear out of the body sprite.
                b.pos = { m_player.x + dx * 0.55f,
                          m_player.y + dy * 0.55f };
                b.vel = { dx * kPlayerBulletV,
                          dy * kPlayerBulletV };
                b.fromPlayer = true;
                m_bullets.push_back(b);
            }
        }
    }

    void UpdateBullets(float dt) {
        for (Bullet& b : m_bullets) {
            if (!b.alive) continue;
            // No lifetime timeout — bullets fly until they hit a wall, the
            // player, or an enemy. The arena's border walls guarantee they
            // always hit something, so a missed shot can't vanish mid-air.
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
                        const int baseDmg = int(kPlayerBulletDmg * m_playerDmgMult + 0.5f);
                        const int  dmg = isCrit ? baseDmg * kCritMult : baseDmg;
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
                            EmitKillPopup(e.pos, m_ctx->kills);
                            EmitDrop(e.pos);
                            // Last-enemy fallback: if no item dropped during
                            // this wave's RNG rolls, force one now so every
                            // wave guarantees at least one box.
                            if (!m_itemThisWave) {
                                bool anyAlive = false;
                                for (const Enemy& o : m_enemies) {
                                    if (o.alive) { anyAlive = true; break; }
                                }
                                if (!anyAlive) {
                                    EmitItem(e.pos, RandomItemKind(m_rng));
                                    m_itemThisWave = true;
                                }
                            }
                        }
                        break;
                    }
                }
            } else {
                const float dx = m_player.x - b.pos.x;
                const float dy = m_player.y - b.pos.y;
                const float rr = kPlayerR + kBulletR;
                if (dx * dx + dy * dy < rr * rr && m_playerHurt <= 0.0f) {
                    m_ctx->playerHp = std::max(0, m_ctx->playerHp - kEnemyBulletDmg);
                    m_playerHurt = kPlayerHurtIFrame;
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
            if (wantDir.x != 0.0f || wantDir.y != 0.0f) e.animTime += dt;
            else                                       e.animTime  = 0.0f;

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

    // Same physics shape as UpdateCoins; pickup dispatches to ApplyItemPickup
    // so a single switch handles every item kind.
    void UpdateItems(float dt, bool pickupEnabled) {
        for (Item& it : m_items) {
            if (!it.alive) continue;
            it.age += dt;
            if (it.age >= kCoinFullLife) { it.alive = false; continue; }
            if (!it.settled) {
                it.vz -= kCoinGravity * dt;
                const float nx = it.pos.x + it.vel.x * dt;
                if (CircleBlockedByWalls(m_grid, nx, it.pos.y, kCoinR))
                    it.vel.x = -it.vel.x * kCoinWallBounce;
                else
                    it.pos.x = nx;
                const float ny = it.pos.y + it.vel.y * dt;
                if (CircleBlockedByWalls(m_grid, it.pos.x, ny, kCoinR))
                    it.vel.y = -it.vel.y * kCoinWallBounce;
                else
                    it.pos.y = ny;
                it.z += it.vz * dt;
                if (it.z <= 0.0f) {
                    it.z = 0.0f;
                    if (-it.vz < kCoinSettleVz) {
                        it.settled = true;
                        it.vz  = 0.0f;
                        it.vel = {0.0f, 0.0f};
                    } else {
                        it.vz    = -it.vz * kCoinBounce;
                        it.vel.x *= kCoinFriction;
                        it.vel.y *= kCoinFriction;
                    }
                }
            }
            if (pickupEnabled) {
                const float dx = it.pos.x - m_player.x;
                const float dy = it.pos.y - m_player.y;
                if (dx * dx + dy * dy < kCoinPickupR * kCoinPickupR) {
                    ApplyItemPickup(it);
                    it.alive = false;
                }
            }
        }
        m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                     [](const Item& it) { return !it.alive; }),
                      m_items.end());
    }

    // pickupEnabled lets the dead branch keep coins bouncing without letting
    // the dead player magically vacuum them.
    void UpdateCoins(float dt, bool pickupEnabled) {
        for (Coin& c : m_coins) {
            if (!c.alive) continue;
            c.age += dt;
            if (c.age >= kCoinFullLife) { c.alive = false; continue; }
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
            // Spin runs every frame — fast while bouncing, slow once settled
            // so the dropped coin keeps its glint without spinning at the
            // mid-air speed.
            c.spin += (c.settled ? kCoinSpinSettled : kCoinSpinSpeed) * dt;
            if (pickupEnabled) {
                const float dx = c.pos.x - m_player.x;
                const float dy = c.pos.y - m_player.y;
                if (dx * dx + dy * dy < kCoinPickupR * kCoinPickupR) {
                    m_ctx->score += kCoinScore;
                    // Small yellow "+N" popup so the player feels the score
                    // tick without having to glance up at the HUD.
                    Popup pp;
                    pp.pos    = c.pos;
                    std::snprintf(pp.text, sizeof(pp.text), "+%d", kCoinScore);
                    pp.color  = Color(1.0f, 0.93f, 0.32f, 1.0f);
                    pp.pxSize = 18.0f;
                    pp.life   = 0.80f;
                    m_popups.push_back(pp);
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
        m_itemThisWave = false;
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
            e.pos   = { ex, ey };
            // Per-wave HP scaling: +kEnemyHpPerWave on top of the base
            // each wave past the first.
            e.hpMax = kEnemyHpMax + (m_ctx->wave - 1) * kEnemyHpPerWave;
            e.hp    = e.hpMax;
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
        for (size_t i = 0; i < m_items.size(); ++i)
            items.push_back({m_items[i].pos.y - 0.001f, 5, int(i)});

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
                    if (ddx * ddx + ddy * ddy >= m_lightRange * m_lightRange) {
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
                case 5: DrawItem(r, m_items[it.index]); break;
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

        const float r2 = m_lightRange * m_lightRange;
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
        DrawShadow(r, {feet.x, feet.y + 6.0f}, 18.0f);

        if (!m_playerTex) {
            // Fallback colored quad if the atlas didn't load.
            r.DrawQuad({feet.x - 14.0f, feet.y - 22.0f}, {28.0f, 18.0f},
                       Color(L, L, L, 1.0f));
            return;
        }

        // Map 8-direction index (0=N clockwise) to the four baked rows
        // (N, NE, SE, S) plus a horizontal flip for the west half.
        struct DirSprite { int row; bool flipX; };
        static const DirSprite kDirMap[8] = {
            {0, false}, // N
            {1, false}, // NE
            {2, false}, // E  (uses SE)
            {2, false}, // SE
            {3, false}, // S
            {2, true},  // SW (flip SE)
            {2, true},  // W  (flip SE — E was made identical to SE)
            {1, true},  // NW (flip NE)
        };
        // Pure world-direction quantisation: 0 = N (world -y), increasing
        // clockwise to E, S, W. No Y compression — the sprite cares about
        // which way the character is facing in world terms, not how the
        // tile is projected.
        const float a   = std::atan2(m_facing.x, -m_facing.y);
        const float aN  = (a < 0.0f) ? a + 6.2831853f : a;
        const int   dir = int(std::round(aN / 6.2831853f * 8.0f)) & 7;
        const DirSprite& sp = kDirMap[dir];

        // Walk cycle: 16 fps loops over the 8-frame row while moving.
        // m_animTime is reset to 0 when WASD is released, so the idle
        // pose is always frame 0.
        constexpr float kAnimFps = 16.0f;
        const int frame = int(m_animTime * kAnimFps) & 7;

        // Source frames are 60×80 px. Render at 48×64 on screen — 64 tall
        // matches the request, 48 wide keeps the 60:80 aspect ratio.
        constexpr float kSpriteW = 48.0f;
        constexpr float kSpriteH = 64.0f;
        // Anchor: bottom-centre of the sprite at the player's feet, with
        // a small upward nudge so the character isn't standing on the
        // floor-shadow line.
        const Vec2 pos{ feet.x - kSpriteW * 0.5f,
                        feet.y - kSpriteH + 4.0f };
        const Color base = (m_playerHurt > 0.0f)
                              ? Color(1.0f, 0.55f, 0.55f, 1.0f)
                              : Color::White();
        const Color tint(base.r * L, base.g * L, base.b * L, 1.0f);
        r.DrawQuad(pos, {kSpriteW, kSpriteH},
                   m_playerFrames[sp.row][frame], tint, sp.flipX);

        // In-world HP bar above the player's head — thick rounded rect with
        // a tier-coloured fill and a centred bold HP number (white text on
        // black outline). Colour tier:
        //   HP ≥ 80 → green
        //   HP ≥ 20 → yellow
        //   else    → red
        const int   hp     = m_ctx->playerHp;
        const float hpFrac = std::max(0.0f, float(hp) / float(kPlayerHpMax));
        const Color fill   = (hp >= 80) ? Color(0.32f, 0.85f, 0.32f, 1.0f)
                           : (hp >= 20) ? Color(0.95f, 0.85f, 0.25f, 1.0f)
                                        : Color(0.95f, 0.30f, 0.30f, 1.0f);
        constexpr float kBarW      = 50.0f;
        constexpr float kBarH      = 14.0f;
        constexpr float kBarRadius = 3.0f;
        const float barX = feet.x - kBarW * 0.5f;
        const float barY = feet.y - kSpriteH - kBarH - 2.0f;
        auto barTint = [L](const Color& c) {
            return Color(c.r * L, c.g * L, c.b * L, c.a);
        };
        // Bold black border underneath the bar — extends a couple of pixels
        // out on every side so it reads as a frame around the whole capsule.
        constexpr float kBarBorder = 2.0f;
        DrawRoundRect(r,
                      {barX - kBarBorder, barY - kBarBorder},
                      {kBarW + kBarBorder * 2.0f, kBarH + kBarBorder * 2.0f},
                      kBarRadius + kBarBorder,
                      barTint(Color(0.0f, 0.0f, 0.0f, 0.95f)));
        DrawRoundRect(r, {barX, barY}, {kBarW, kBarH}, kBarRadius,
                      barTint(Color(0.12f, 0.06f, 0.06f, 0.95f)));
        const float fillW = kBarW * hpFrac;
        if (fillW > 0.5f) {
            DrawRoundRect(r, {barX, barY}, {fillW, kBarH}, kBarRadius,
                          barTint(fill));
        }
        // Bold HP number — fake-bold via 8-direction offset stroke,
        // black outline + white centre so the digit reads on any fill
        // colour.
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", hp);
        constexpr float textPx = 12.0f;
        const Vec2 ts = m_ctx->font->Measure(buf, textPx);
        const float textX = feet.x - ts.x * 0.5f;
        const float textY = barY + (kBarH - textPx) * 0.5f - 1.0f;
        constexpr float kOff = 1.2f;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                r.DrawText(*m_ctx->font, buf,
                           {textX + dx * kOff, textY + dy * kOff},
                           barTint(Color(0.0f, 0.0f, 0.0f, 1.0f)), textPx);
            }
        }
        r.DrawText(*m_ctx->font, buf, {textX, textY},
                   barTint(Color(1.0f, 1.0f, 1.0f, 1.0f)), textPx);
    }

    void DrawEnemy(Renderer2D& r, const Enemy& e) {
        const Vec2 feet = W2S(e.pos, m_camOrigin);
        const float L = SampleShadowAt(e.pos);
        DrawShadow(r, {feet.x, feet.y + 5.0f}, 16.0f);

        if (m_enemyTex) {
            // 8-direction → 3-row map. NE shares the N row, SE shares the
            // S row; the west half mirrors via flipX.
            static const struct DirSprite { int row; bool flipX; } kDirMap[8] = {
                {0, false}, // N
                {0, false}, // NE  (uses N)
                {1, false}, // E
                {2, false}, // SE  (uses S)
                {2, false}, // S
                {2, true},  // SW  (flip S)
                {1, true},  // W   (flip E)
                {0, true},  // NW  (flip N)
            };
            const float a   = std::atan2(e.aim.x, -e.aim.y);
            const float aN  = (a < 0.0f) ? a + 6.2831853f : a;
            const int   dir = int(std::round(aN / 6.2831853f * 8.0f)) & 7;
            const DirSprite& sp = kDirMap[dir];
            constexpr float kAnimFps = 10.0f;        // 4-frame loop in 0.4 s
            const int frame = int(e.animTime * kAnimFps) & 3;
            constexpr float kSpriteW = 48.0f;
            constexpr float kSpriteH = 56.0f;
            const Vec2 pos{ feet.x - kSpriteW * 0.5f,
                            feet.y - kSpriteH + 4.0f };
            // Hurt flash brightens by tinting > 1.0 (the framebuffer
            // clamps each channel to 1.0, so the sprite washes out white).
            const Color base = (e.hurtFlash > 0.0f)
                                  ? Color(2.5f, 2.5f, 2.5f, 1.0f)
                                  : Color(1.0f, 1.0f, 1.0f, 1.0f);
            const Color tint(base.r * L, base.g * L, base.b * L, base.a);
            r.DrawQuad(pos, {kSpriteW, kSpriteH},
                       m_enemyFrames[sp.row][frame], tint, sp.flipX);
        }

        // HP bar above head — continuous fill scaled by hp/hpMax. Tinted by
        // the shadow value so a hidden enemy doesn't give itself away in
        // the dark.
        const float barW = 26.0f;
        const float barH = 3.0f;
        const float hpFrac = std::max(0.0f,
            float(e.hp) / float(std::max(1, e.hpMax)));
        auto tintC = [L](const Color& c) {
            return Color(c.r * L, c.g * L, c.b * L, c.a);
        };
        r.DrawQuad({feet.x - barW * 0.5f, feet.y - 56.0f},
                   {barW, barH},
                   tintC(Color(0.20f, 0.10f, 0.10f, 0.85f)));
        r.DrawQuad({feet.x - barW * 0.5f, feet.y - 56.0f},
                   {barW * hpFrac, barH},
                   tintC(Color(0.95f, 0.30f, 0.30f, 1.0f)));
    }

    // In auto-aim mode, show the player what's happening:
    //   * a faint cone wedge along m_moveDir so they can see which way the
    //     auto-aim is searching;
    //   * a pulsing reticle on the current lock target so the next bullet's
    //     destination is obvious.
    void DrawAutoAimOverlay(Renderer2D& r) {
        // No gameplay UI on top of the dying screen — the lock state is
        // frozen at death so the reticle would otherwise hang in the dark
        // until the player restarts.
        if (m_dead) return;
        const Vec2 pScreen = W2S(m_player, m_camOrigin);

        // Single textured pie. The sprite's wedge centre points along +x in
        // texture space; rotating by the screen-space angle of m_moveDir
        // aligns it with where bullets actually travel under Y compression.
        // The quad's centre = the wedge apex (=player), so the visible cone
        // reaches kRadius pixels from the player.
        const float ang = WorldDirToScreenAngle(m_moveDir);
        constexpr float kRadius = 200.0f;
        const Vec2 origin{ pScreen.x, pScreen.y - 14.0f };  // raise off feet
        if (m_pieTex) {
            r.DrawQuad(origin, {kRadius * 2.0f, kRadius * 2.0f}, ang,
                       m_pieRegion,
                       Color(1.0f, 0.95f, 0.30f, 0.05f));
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

        // About-to-vanish blink: between kCoinIdleLife and kCoinFullLife the
        // coin pulses on/off at kCoinBlinkRate Hz so the player can see it's
        // expiring without having to track a counter.
        float alphaMod = 1.0f;
        if (c.age >= kCoinIdleLife) {
            const float blinkT = c.age - kCoinIdleLife;
            // Multiply by 2 so each int step is a half-cycle (on or off).
            alphaMod = (int(blinkT * kCoinBlinkRate * 2.0f) & 1) ? 0.18f : 1.0f;
        }

        // Floor shadow shrinks as the coin rises — a tiny depth cue.
        const float shadeShrink = 1.0f / (1.0f + c.z * 0.6f);
        const float shadeW = 10.0f * shadeShrink;
        const float shadeH = 3.0f  * shadeShrink;
        r.DrawQuad({feet.x - shadeW * 0.5f, feet.y - shadeH * 0.5f},
                   {shadeW, shadeH},
                   Color(0.0f, 0.0f, 0.0f, 0.35f * L * alphaMod));

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
            const Color tint(L, L, L, alphaMod);
            r.DrawQuad({sp.x - kCoinSpriteW * 0.5f, sp.y - kCoinSpriteH * 0.5f},
                       {kCoinSpriteW, kCoinSpriteH},
                       m_coinFrames[frame], tint);
        }
    }

    // Filled rounded rectangle built from horizontal scanlines: the central
    // body is a single quad, the rounded caps top + bottom are stacks of
    // 1 px scanlines whose left/right cuts follow a quarter-circle so the
    // edge reads as a curve instead of a step.
    void DrawRoundRect(Renderer2D& r, Vec2 pos, Vec2 size,
                       float radius, const Color& c) {
        const int rad = std::max(0, int(radius));
        // Centre body — full width, height shrunk by 2*rad.
        if (size.y > 2.0f * rad) {
            r.DrawQuad({pos.x, pos.y + float(rad)},
                       {size.x, size.y - 2.0f * float(rad)}, c);
        }
        // Top and bottom caps. The top cap's row 0 sits at the very top edge
        // (far from the arc centre), and the bottom cap's row 0 sits just
        // below the body (close to the arc centre) — so the distance-from-
        // centre used for the chord calculation has to be computed
        // separately for the two caps.
        const float r2 = float(rad) * float(rad);
        for (int i = 0; i < rad; ++i) {
            const float dyTop = float(rad) - 0.5f - float(i);  // i=0 → top edge
            const float dyBot = float(i) + 0.5f;               // i=0 → near body
            const float chordTop = std::sqrt(std::max(0.0f, r2 - dyTop * dyTop));
            const float chordBot = std::sqrt(std::max(0.0f, r2 - dyBot * dyBot));
            const float cutTop = float(rad) - chordTop;
            const float cutBot = float(rad) - chordBot;
            r.DrawQuad({pos.x + cutTop, pos.y + float(i)},
                       {size.x - 2.0f * cutTop, 1.0f}, c);
            r.DrawQuad({pos.x + cutBot, pos.y + size.y - float(rad) + float(i)},
                       {size.x - 2.0f * cutBot, 1.0f}, c);
        }
    }

    // Item: gray-bordered black rounded square with a coloured letter glyph
    // in the centre. Same bouncing / blink / shadow handling as a coin.
    // Visual is uniform — only the letter and its colour change per kind —
    // so adding a new item just needs ItemKind + ItemDefOf + ApplyItemPickup.
    void DrawItem(Renderer2D& r, const Item& it) {
        const Vec2 feet = W2S(it.pos, m_camOrigin);
        const float L = SampleShadowAt(it.pos);

        float alphaMod = 1.0f;
        if (it.age >= kCoinIdleLife) {
            const float blinkT = it.age - kCoinIdleLife;
            alphaMod = (int(blinkT * kCoinBlinkRate * 2.0f) & 1) ? 0.18f : 1.0f;
        }

        // Floor shadow shrinks with z.
        const float shadeShrink = 1.0f / (1.0f + it.z * 0.6f);
        const float shadeW = 18.0f * shadeShrink;
        const float shadeH = 5.0f  * shadeShrink;
        r.DrawQuad({feet.x - shadeW * 0.5f, feet.y - shadeH * 0.5f},
                   {shadeW, shadeH},
                   Color(0.0f, 0.0f, 0.0f, 0.40f * L * alphaMod));

        const float liftPx = it.z * kTileH;
        const Vec2 sp{ feet.x, feet.y - liftPx };
        const ItemDef def = ItemDefOf(it.kind);
        const float box = kItemBoxPx;
        const float bw  = kItemBorderPx;
        auto tintC = [L, alphaMod](const Color& c) {
            return Color(c.r * L, c.g * L, c.b * L, c.a * alphaMod);
        };
        const Color gray (0.55f, 0.55f, 0.55f, 1.0f);
        const Color black(0.06f, 0.06f, 0.06f, 1.0f);
        // Outer gray frame.
        DrawRoundRect(r, {sp.x - box * 0.5f, sp.y - box * 0.5f},
                      {box, box}, kItemBoxRadius, tintC(gray));
        // Inner black panel, inset by the border thickness.
        DrawRoundRect(r,
                      {sp.x - box * 0.5f + bw, sp.y - box * 0.5f + bw},
                      {box - 2.0f * bw, box - 2.0f * bw},
                      kItemBoxRadius - 1.0f, tintC(black));
        // Centred coloured letter — slight upward nudge so the optical
        // centre of the glyph sits on the box centre.
        const char letterStr[2] = { def.letter, 0 };
        const float letterPx = box * 0.72f;
        const Vec2 ts = m_ctx->font->Measure(letterStr, letterPx);
        r.DrawText(*m_ctx->font, letterStr,
                   {sp.x - ts.x * 0.5f, sp.y - letterPx * 0.5f - 1.0f},
                   tintC(def.color), letterPx);
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

    // Pause menu data + layout. Adding a menu entry: append to the array
    // and add a case to ActivatePauseMenu.
    static constexpr const char* kPauseMenuLabels[] = {
        "Resume", "Surrender", "Exit",
    };
    static constexpr int   kPauseMenuCount = 3;
    static constexpr float kPauseMenuTopY  = 280.0f;   // y of first item centre
    static constexpr float kPauseMenuStep  = 70.0f;    // vertical pitch
    static constexpr float kPauseHitHalf   = 30.0f;    // mouse hit ½-height

    // Activate menu index — pulled out so both Enter and click can fire it.
    void ActivatePauseMenu(int idx) {
        switch (idx) {
            case 0:  m_paused = false; break;                  // Resume
            case 1:  Manager().Change(m_ctx->makeTitle()); break;  // Surrender
            case 2:  Application::Get().Close(); break;         // Exit
            default: break;
        }
    }

    // Returns the menu index whose Y row the mouse is in, or -1 if not over
    // any item. Y-only test — the rows are wide so X bounds aren't needed.
    int PauseMenuItemAtMouse(Vec2 m) const {
        if (m.x < kScreenW * 0.20f || m.x > kScreenW * 0.80f) return -1;
        for (int i = 0; i < kPauseMenuCount; ++i) {
            const float yC = kPauseMenuTopY + i * kPauseMenuStep;
            if (m.y >= yC - kPauseHitHalf && m.y <= yC + kPauseHitHalf)
                return i;
        }
        return -1;
    }

    // Filled right-pointing triangle drawn as vertical 1 px columns whose
    // height shrinks linearly from `h` at the left edge to 0 at the apex.
    // The MSDF font doesn't include ▶ (U+25B6), so we build the indicator
    // out of axis-aligned quads instead.
    void DrawRightTri(Renderer2D& r, Vec2 leftMidpoint,
                      float w, float h, const Color& c) {
        const int N = int(w);
        for (int x = 0; x < N; ++x) {
            const float colH = h * (1.0f - float(x) / w);
            if (colH <= 0.5f) break;
            r.DrawQuad({leftMidpoint.x + float(x),
                        leftMidpoint.y - colH * 0.5f},
                       {1.0f, colH}, c);
        }
    }

    void DrawPauseOverlay(Renderer2D& r) {
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH},
                   Color(0.0f, 0.0f, 0.0f, 0.45f));
        DrawCenteredText(r, *m_ctx->font, "PAUSED",
                         140.0f, 76.0f,
                         Color(1.0f, 1.0f, 1.0f, 1.0f));
        for (int i = 0; i < kPauseMenuCount; ++i) {
            const float yC = kPauseMenuTopY + i * kPauseMenuStep;
            const bool sel = (i == m_pauseMenuIdx);
            const Color col = sel ? Color(1.0f, 0.95f, 0.30f, 1.0f)
                                  : Color(0.70f, 0.72f, 0.74f, 1.0f);
            const float px = sel ? 44.0f : 36.0f;
            const Vec2 ts = m_ctx->font->Measure(kPauseMenuLabels[i], px);
            const float textX = (kScreenW - ts.x) * 0.5f;
            const float textY = yC - px * 0.50f;
            if (sel) {
                // Place the triangle's left edge 24 px to the left of the
                // text, vertically centred on the item's row.
                DrawRightTri(r, {textX - 36.0f, yC}, 18.0f, 24.0f, col);
            }
            r.DrawText(*m_ctx->font, kPauseMenuLabels[i],
                       {textX, textY}, col, px);
        }
        DrawCenteredText(r, *m_ctx->font,
                         "Arrow keys / Mouse to select   Enter / Click to confirm",
                         kScreenH * 0.85f, 20.0f,
                         Color(0.80f, 0.80f, 0.80f, 0.85f));
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
        // Top bar (player HP now lives above the player's head in DrawPlayer).
        r.DrawQuad({0, 0}, {kScreenW, 48.0f}, kHudBg);

        char buf[64];
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

        // Shadow toggle is debug-only (F8). Only show a HUD pill when
        // shadows have been turned off, so the player isn't tempted to use
        // it but a developer can see at a glance that they've flipped it.
        if (!m_ctx->shadowsOn) {
            r.DrawQuad({16.0f, 88.0f}, {200.0f, 26.0f},
                       Color(0.0f, 0.0f, 0.0f, 0.50f));
            r.DrawText(*m_ctx->font, "SHADOWS: OFF  (debug)",
                       {26.0f, 92.0f},
                       Color(0.65f, 0.65f, 0.65f, 1.0f), 20.0f);
        }

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
    Vec2    m_camOrigin{0.0f, 0.0f};
    float   m_playerHurt = 0.0f;
    float   m_fireCd     = 0.0f;
    // HP regen accumulator. While the player is stationary (no WASD input
    // this frame) it grows at kPlayerRegenRate per second; once it crosses
    // 1.0 it pops an integer +HP and the surplus carries over. Reset to 0
    // on death; the displayed HP value stays the integer count.
    float   m_hpRegenAccum = 0.0f;
    // Auto-aim solve from this frame's UpdatePlayer. Cached so DrawHud and
    // the reticle render can use it without re-scanning the enemy list.
    bool    m_autoLocked = false;
    Vec2    m_autoTargetPos{0.0f, 0.0f};
    float   m_autoPulse = 0.0f;     // reticle blink
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
    // Pre-baked pie/wedge sprite for the auto-aim cone overlay. Built once
    // from kAutoAimConeCos so the bevel and radial fade are smooth and a
    // single textured quad replaces the 24-ray fan that showed banding.
    std::unique_ptr<RHITexture> m_pieTex;
    TextureRegion               m_pieRegion;
    // Coin sprite sheet (5×2 frames). Loaded once per scene from
    // assets/coin.png; m_coinFrames[i] points at frame i in cell-row order.
    std::unique_ptr<RHITexture> m_coinTex;
    TextureRegion               m_coinFrames[kCoinFrameCount];
    // Player atlas: 4 directional rows × 8 walk frames at 60×80 each.
    // Rows in order: N (back), NE, SE, S (front). E uses SE; W/SW use SE
    // mirrored; NW uses NE mirrored. Driven by m_facing → 8-direction
    // index, animated by m_animTime while WASD is held.
    std::unique_ptr<RHITexture> m_playerTex;
    TextureRegion               m_playerFrames[4][8];
    float                       m_animTime = 0.0f;
    // Enemy atlas: 3 directional rows × 4 walk frames at 48×56 each.
    // Rows in order: N, E, S. NE uses N; SE uses S; W/SW/NW are flipped.
    std::unique_ptr<RHITexture> m_enemyTex;
    TextureRegion               m_enemyFrames[3][4];
    int     m_shadowW = 0, m_shadowH = 0;
    Vec2    m_lastShadowSrc{1e9f, 1e9f};          // forces first-frame rebuild
    float   m_shadowFlash = 0.0f;                 // HUD flash after H toggle
    std::vector<Bullet>        m_bullets;
    std::vector<Enemy>         m_enemies;
    std::vector<StarParticle>  m_particles;
    std::vector<Popup>         m_popups;
    std::vector<Coin>          m_coins;
    std::vector<Item>          m_items;
    // Player power-ups picked up during this run. Reset on every new run.
    // Damage multiplier is additive (1.5 → 2.0 → 2.5 over repeated Powers);
    // fire-rate multiplier is multiplicative (each Speed item doubles the
    // rate, so the cooldown becomes kPlayerFireCd / rateMult).
    float                      m_playerDmgMult   = 1.0f;
    float                      m_playerRateMult  = 1.0f;
    int                        m_playerShotCount = 1;   // bullets per fire; +1 per Multi pickup, capped at kMultiMaxShots
    bool    m_waveActive = false;
    float   m_waveTimer  = 0.0f;
    // Latched true the first time an item drops in the current wave; if
    // still false when the last enemy of the wave dies, we force an item
    // drop so every wave guarantees at least one box.
    bool    m_itemThisWave = false;
    // True once the player's HP hits zero. We stay in this scene instead of
    // pushing a GameOver overlay: gameplay updates pause, particles keep
    // animating, and a blinking GAME OVER text waits for Enter/Esc.
    bool    m_dead       = false;
    float   m_deathPulse = 0.0f;
    // Effective light disc radius; equals kLightRange while alive and
    // shrinks to 0 over the first ~2.5s after death so the visible world
    // closes in on the player before the GAME OVER overlay covers it.
    float   m_lightRange = kLightRange;
    // True while the player has hit Esc mid-run. Freezes all gameplay
    // updates and dims the screen with an overlay.
    bool    m_paused     = false;
    float   m_pausePulse = 0.0f;
    int     m_pauseMenuIdx = 0;     // 0..kPauseMenuCount-1, current selection
    Vec2    m_pauseLastMouse{};     // last-frame mouse pos; hover only fires on move

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
