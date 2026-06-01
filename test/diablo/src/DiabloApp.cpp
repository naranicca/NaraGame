// Diablo-style isometric action RPG -- v1 foundation.
//
// This commit is the SCAFFOLD: title -> town -> dungeon scene flow, iso
// projection + depth sort, 8-directional WASD movement, and a procedural
// dungeon that re-rolls on every entry. Combat, monsters, and items land
// in the next pass.
//
// Controls:
//   Title scene:    Enter start
//   In world:       WASD / arrows move (8-directional)
//                   E interact (portal in town -> dungeon, stairs in dungeon -> town)
//                   Esc title (returns to title scene)
//
// Layout notes:
//   World tile is 64x32 (2:1 iso diamond). World coords (tx, ty) are
//   row/col into the level grid; screen pos is the standard iso projection.
//   Player position is FLOAT in tile space so motion is smooth between cells.

#include <NaraGame/assets/MSDFFontLoader.h>
#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/debug/DebugDraw.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/Camera2D.h>
#include <NaraGame/renderer/MSDFFont.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/TextureAtlas.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>
#include <NaraGame/scene/Scene.h>
#include <NaraGame/scene/SceneManager.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
// Iso projection constants.
// ---------------------------------------------------------------------------
constexpr float kTileW     = 64.0f;
constexpr float kTileH     = 32.0f;
constexpr float kHalfTileW = kTileW * 0.5f;
constexpr float kHalfTileH = kTileH * 0.5f;
constexpr float kWallH     = 64.0f;   // vertical face height in pixels

constexpr float kScreenW = 1024.0f;
constexpr float kScreenH = 640.0f;

constexpr float kPlayerSpeed = 3.5f; // tiles per second

// Sprite atlas regions (must match _make_sprites.ps1 layout).
constexpr float kFloorTileH  = 32.0f;
constexpr float kWallSpriteH = 96.0f;
// Player + monster sit on a 32x48 grid. The earlier 64x96 player draw made
// the figure tower over the rest of the cast; the facing distinction comes
// from the sprite art itself (front face, back-of-head, side profile)
// instead of frame size.
constexpr float kPlayerSpriteW  = 32.0f;
constexpr float kPlayerSpriteH  = 48.0f;
constexpr float kMonsterSpriteW = 32.0f;
constexpr float kMonsterSpriteH = 48.0f;

// ---------------------------------------------------------------------------
// Iso math.
//   Tile (tx, ty) center on screen = origin + (tx - ty) * halfW, (tx + ty) * halfH.
// The origin shifts with the camera so the player stays roughly centered.
// ---------------------------------------------------------------------------
inline Vec2 TileToScreen(float tx, float ty, Vec2 origin) {
    return { origin.x + (tx - ty) * kHalfTileW,
             origin.y + (tx + ty) * kHalfTileH };
}

// ---------------------------------------------------------------------------
// World cell types. The dungeon generator and the town builder both fill in
// a Vec2D<Tile> -- same renderer eats both.
// ---------------------------------------------------------------------------
enum class Tile : uint8_t {
    Empty    = 0,  // off-map / hole. Not drawn.
    Floor    = 1,  // dungeon stone
    Grass    = 2,  // town
    Dirt     = 3,  // town path
    Wall     = 4,  // short half-height wall. Blocks movement + sight.
    Portal   = 5,  // interact -> enter dungeon
    Stairs   = 6,  // interact -> return to town
};

bool TileBlocksMovement(Tile t) {
    return t == Tile::Wall || t == Tile::Empty;
}

bool TileIsTall(Tile t) {
    return t == Tile::Wall || t == Tile::Portal;
}

bool TileBlocksSight(Tile t) {
    return t == Tile::Wall;
}

struct Grid {
    int w = 0, h = 0;
    std::vector<Tile> tiles;

    void Resize(int gw, int gh, Tile fill = Tile::Empty) {
        w = gw; h = gh;
        tiles.assign(static_cast<size_t>(gw) * gh, fill);
    }
    Tile At(int x, int y) const {
        if (x < 0 || y < 0 || x >= w || y >= h) return Tile::Empty;
        return tiles[static_cast<size_t>(y) * w + x];
    }
    void Set(int x, int y, Tile t) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        tiles[static_cast<size_t>(y) * w + x] = t;
    }
};

// ---------------------------------------------------------------------------
// Dungeon generator -- random rooms connected by L-shaped corridors.
// Seeded per entry so each visit is a different layout. We also report the
// player spawn (inside the first room) and the stairs position (last room).
// ---------------------------------------------------------------------------
struct DungeonGenResult {
    Grid grid;
    Vec2 playerSpawn{};   // tile-space center
    Vec2 stairsTile{};
    // Candidate monster spawn tiles -- floor cells inside non-first rooms.
    // The dungeon scene picks N of these at random and places monsters.
    std::vector<Vec2> monsterSpawns;
};

DungeonGenResult GenerateDungeon(uint32_t seed, int gw = 40, int gh = 40) {
    std::mt19937 rng(seed);
    DungeonGenResult r;
    r.grid.Resize(gw, gh, Tile::Empty);

    struct Room { int x, y, w, h; };
    std::vector<Room> rooms;
    const int kAttempts = 60;
    std::uniform_int_distribution<int> sizeDist(5, 9);
    std::uniform_int_distribution<int> posXDist(1, gw - 2);
    std::uniform_int_distribution<int> posYDist(1, gh - 2);
    for (int i = 0; i < kAttempts; ++i) {
        Room rm;
        rm.w = sizeDist(rng);
        rm.h = sizeDist(rng);
        rm.x = posXDist(rng) % std::max(1, gw - rm.w - 1) + 1;
        rm.y = posYDist(rng) % std::max(1, gh - rm.h - 1) + 1;
        // Reject overlap with margin so corridors don't fuse rooms.
        bool overlap = false;
        for (const Room& o : rooms) {
            if (rm.x <= o.x + o.w && o.x <= rm.x + rm.w &&
                rm.y <= o.y + o.h && o.y <= rm.y + rm.h) { overlap = true; break; }
        }
        if (overlap) continue;
        rooms.push_back(rm);
        if (rooms.size() >= 9) break;
    }
    if (rooms.empty()) {
        // Fallback: a single room dead center so the level is always playable
        // even if the rejection sampler had a bad seed.
        rooms.push_back({gw / 2 - 4, gh / 2 - 4, 8, 8});
    }

    // Carve floors for every room.
    for (const Room& rm : rooms) {
        for (int y = rm.y; y < rm.y + rm.h; ++y)
            for (int x = rm.x; x < rm.x + rm.w; ++x)
                r.grid.Set(x, y, Tile::Floor);
    }

    // Connect rooms in order with L-shaped corridors. Each segment is two
    // tiles wide -- single-tile corridors looked claustrophobic and the
    // player can't sidestep monsters in them. CarveLineX extends downward
    // (y+1), CarveLineY extends rightward (x+1); at an L corner both lines
    // overlap into a 2x2 elbow so the turn isn't pinched.
    auto CarveLineX = [&](int y, int x0, int x1) {
        if (x0 > x1) std::swap(x0, x1);
        for (int x = x0; x <= x1; ++x) {
            r.grid.Set(x, y,     Tile::Floor);
            r.grid.Set(x, y + 1, Tile::Floor);
        }
    };
    auto CarveLineY = [&](int x, int y0, int y1) {
        if (y0 > y1) std::swap(y0, y1);
        for (int y = y0; y <= y1; ++y) {
            r.grid.Set(x,     y, Tile::Floor);
            r.grid.Set(x + 1, y, Tile::Floor);
        }
    };
    std::uniform_int_distribution<int> coinDist(0, 1);
    for (size_t i = 1; i < rooms.size(); ++i) {
        const Room& a = rooms[i - 1];
        const Room& b = rooms[i];
        const int ax = a.x + a.w / 2;
        const int ay = a.y + a.h / 2;
        const int bx = b.x + b.w / 2;
        const int by = b.y + b.h / 2;
        if (coinDist(rng) == 0) {
            CarveLineX(ay, ax, bx);
            CarveLineY(bx, ay, by);
        } else {
            CarveLineY(ax, ay, by);
            CarveLineX(by, ax, bx);
        }
    }

    // Walls: every cell adjacent to a floor that ISN'T a floor becomes a wall.
    // Border cells too -- this gives the dungeon a contained look.
    Grid walls = r.grid;
    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
            if (r.grid.At(x, y) != Tile::Empty) continue;
            bool nearFloor = false;
            for (int dy = -1; dy <= 1 && !nearFloor; ++dy) {
                for (int dx = -1; dx <= 1 && !nearFloor; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (r.grid.At(x + dx, y + dy) == Tile::Floor) nearFloor = true;
                }
            }
            if (nearFloor) walls.Set(x, y, Tile::Wall);
        }
    }
    r.grid = std::move(walls);

    // Drop a few interior pillars inside the larger rooms (skip the spawn
    // room so the player isn't penned in immediately). All walls in the
    // dungeon use the same short-wall sprite, so the pillars and the
    // perimeter both render with the same look.
    for (size_t i = 1; i < rooms.size(); ++i) {
        const Room& rm = rooms[i];
        const int area = rm.w * rm.h;
        if (area < 25) continue;
        const int n = (area > 49) ? 2 : 1;
        std::uniform_int_distribution<int> rx(rm.x + 1, rm.x + rm.w - 2);
        std::uniform_int_distribution<int> ry(rm.y + 1, rm.y + rm.h - 2);
        for (int k = 0; k < n; ++k) {
            const int x = rx(rng), y = ry(rng);
            if (r.grid.At(x, y) == Tile::Floor)
                r.grid.Set(x, y, Tile::Wall);
        }
    }

    // Spawn inside the first room; stairs centered in the last room.
    const Room& first = rooms.front();
    const Room& last  = rooms.back();
    r.playerSpawn = { first.x + first.w * 0.5f, first.y + first.h * 0.5f };
    r.stairsTile  = { (float)(last.x + last.w / 2), (float)(last.y + last.h / 2) };
    r.grid.Set((int)r.stairsTile.x, (int)r.stairsTile.y, Tile::Stairs);

    // Monster spawns: every floor tile inside a non-first room, except the
    // stairs cell. Caller picks how many to place. We skip the first room so
    // the player gets one "safe" room to find their bearings on entry.
    for (size_t i = 1; i < rooms.size(); ++i) {
        const Room& rm = rooms[i];
        for (int y = rm.y; y < rm.y + rm.h; ++y) {
            for (int x = rm.x; x < rm.x + rm.w; ++x) {
                if (r.grid.At(x, y) != Tile::Floor) continue;
                r.monsterSpawns.push_back({(float)x, (float)y});
            }
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// Town: a small open patch of grass with a dirt road, surrounded by walls,
// with a portal cell near the center-south.
// ---------------------------------------------------------------------------
struct TownResult {
    Grid grid;
    Vec2 playerSpawn{};
    Vec2 portalTile{};
};

TownResult BuildTown() {
    TownResult t;
    // 60x60 grid -- ~9x the 20x20 footprint; player has somewhere to walk to.
    const int gw = 60, gh = 60;
    t.grid.Resize(gw, gh, Tile::Empty);
    // Grass interior, bordered by walls.
    for (int y = 1; y < gh - 1; ++y)
        for (int x = 1; x < gw - 1; ++x)
            t.grid.Set(x, y, Tile::Grass);
    for (int x = 0; x < gw; ++x) {
        t.grid.Set(x, 0, Tile::Wall);
        t.grid.Set(x, gh - 1, Tile::Wall);
    }
    for (int y = 0; y < gh; ++y) {
        t.grid.Set(0, y, Tile::Wall);
        t.grid.Set(gw - 1, y, Tile::Wall);
    }
    // Dirt path runs from the south wall all the way up to the portal at
    // the middle of the map. Three tiles wide so the player has elbow room.
    const int cx = gw / 2;
    for (int y = gh - 2; y >= gh / 2 + 1; --y) {
        t.grid.Set(cx, y, Tile::Dirt);
        t.grid.Set(cx - 1, y, Tile::Dirt);
        t.grid.Set(cx + 1, y, Tile::Dirt);
    }
    // Portal at the path's end.
    t.portalTile = { (float)cx, (float)(gh / 2) };
    t.grid.Set(cx, gh / 2, Tile::Portal);
    // Player spawns at the path's south end.
    t.playerSpawn = { (float)cx, (float)(gh - 3) };
    return t;
}

// ---------------------------------------------------------------------------
// Items.
//   Three base kinds: Sword (boosts player damage), Armor (boosts max HP),
//   Potion (one-shot heal). Sword/Armor roll a rarity (Common/Magic/Rare)
//   with progressively bigger stats and a prefix word picked from a rarity-
//   keyed table -- the Diablo affix convention shrunk to one slot. Potions
//   are always Common and always heal a fixed amount.
//
//   Items live in two places:
//     * GroundItem -- on the dungeon floor, anchored at tile coords,
//       picked up by walking near.
//     * GameContext::inventory -- in the player's bag, accessed via the
//       inventory panel (press I).
//   Equipped pieces sit in their own GameContext slots; the inventory list
//   only holds NON-equipped items, so a swap is push-old / pop-new and
//   never shuffles a grid.
// ---------------------------------------------------------------------------
enum class ItemKind  : uint8_t { Sword, Armor, Potion };
enum class Rarity    : uint8_t { Common, Magic, Rare };

struct Item {
    ItemKind kind = ItemKind::Sword;
    Rarity rarity = Rarity::Common;
    std::string name;
    int damage     = 0;   // sword bonus
    int maxHpBonus = 0;   // armor bonus
    int healAmount = 0;   // potion only
};

inline Color RarityColor(Rarity r) {
    switch (r) {
        case Rarity::Common: return Color(0.85f, 0.85f, 0.85f, 1.0f);
        case Rarity::Magic:  return Color(0.40f, 0.65f, 1.00f, 1.0f);
        case Rarity::Rare:   return Color(1.00f, 0.85f, 0.20f, 1.0f);
    }
    return Color::White();
}

inline const char* RarityName(Rarity r) {
    switch (r) {
        case Rarity::Common: return "Common";
        case Rarity::Magic:  return "Magic";
        case Rarity::Rare:   return "Rare";
    }
    return "?";
}

// Item generator: 60% Common, 30% Magic, 10% Rare for gear. Potions are a
// separate roll handled by the caller (drops weight them differently).
Item GenerateGearItem(std::mt19937& rng) {
    static const char* kCommonPrefixes[2] = {"Sharp", "Sturdy"};
    static const char* kMagicPrefixes[3]  = {"Keen",  "Bronze", "Iron"};
    static const char* kRarePrefixes[3]   = {"Mighty","Heroic", "Champion"};

    std::uniform_int_distribution<int> kindRoll(0, 1);
    std::uniform_int_distribution<int> rarityRoll(0, 99);
    const int rr = rarityRoll(rng);
    const Rarity rarity = (rr < 60) ? Rarity::Common
                       : (rr < 90) ? Rarity::Magic
                                   : Rarity::Rare;

    Item it;
    it.kind = (kindRoll(rng) == 0) ? ItemKind::Sword : ItemKind::Armor;
    it.rarity = rarity;

    const char* prefix = nullptr;
    int baseLo = 0, baseHi = 0;
    if (rarity == Rarity::Common) {
        prefix = kCommonPrefixes[std::uniform_int_distribution<int>(0, 1)(rng)];
        baseLo = 1; baseHi = 2;
    } else if (rarity == Rarity::Magic) {
        prefix = kMagicPrefixes[std::uniform_int_distribution<int>(0, 2)(rng)];
        baseLo = 3; baseHi = 5;
    } else {
        prefix = kRarePrefixes[std::uniform_int_distribution<int>(0, 2)(rng)];
        baseLo = 6; baseHi = 10;
    }

    std::uniform_int_distribution<int> rollStat(baseLo, baseHi);
    const int stat = rollStat(rng);
    if (it.kind == ItemKind::Sword) {
        it.damage = stat;
        it.name = std::string(prefix) + " Sword";
    } else {
        // Armor scales HP bigger than sword scales dmg -- a +1 sword is a
        // ~12% damage uplift, +1 HP would be invisible; multiply by 5 to
        // keep the two slots roughly equivalent.
        it.maxHpBonus = stat * 5;
        it.name = std::string(prefix) + " Armor";
    }
    return it;
}

Item MakePotion() {
    Item it;
    it.kind = ItemKind::Potion;
    it.rarity = Rarity::Common;
    it.name = "Health Potion";
    it.healAmount = 25;
    return it;
}

struct GroundItem {
    Item item;
    Vec2 pos{};   // tile-space
    float bobPhase = 0.0f;  // simple float animation -- decorative only
};

// ---------------------------------------------------------------------------
// Monster.
//   Kind drives the sprite + the base stats; everything else is per-instance.
//   Stored as a flat vector on DungeonScene -- combat code iterates the list
//   each frame doing distance checks, which is fine for the ~15 monsters a
//   dungeon holds.
// ---------------------------------------------------------------------------
struct Monster {
    enum class Kind { Skeleton, Zombie };
    Kind kind = Kind::Skeleton;
    Vec2 pos{};
    float hp = 1.0f;
    float maxHp = 1.0f;
    float damage = 1.0f;
    float speed = 1.0f;            // tiles per second
    float attackCooldown = 0.0f;   // ticks down each frame, attack at 0
    float hurtFlashTimer = 0.0f;   // sprite tints white while > 0
    bool alive = true;

    static Monster Make(Kind k, Vec2 p) {
        Monster m;
        m.kind = k;
        m.pos = p;
        if (k == Kind::Skeleton) {
            m.maxHp = 12.0f; m.damage = 4.0f; m.speed = 1.8f;
        } else {
            m.maxHp = 20.0f; m.damage = 6.0f; m.speed = 1.3f;
        }
        m.hp = m.maxHp;
        return m;
    }
};

// ---------------------------------------------------------------------------
// Shared context: scene factories + cross-scene state (sprite atlas, seed,
// dungeon visit count for the title HUD, etc).
// ---------------------------------------------------------------------------
struct GameContext {
    Renderer2D* renderer = nullptr;
    RHIDevice*  device   = nullptr;  // for per-scene textures (e.g. fog map)
    MSDFFont*   font     = nullptr;
    TextureAtlas* sprites = nullptr;
    float screenW = kScreenW;
    float screenH = kScreenH;

    uint32_t rngSeed     = 1;     // bumps on every dungeon entry
    int      dungeonVisits = 0;

    // Player stats persist across town <-> dungeon swaps but reset to full on
    // death (the town acts as a respawn / safe area). Effective damage/HP
    // include equipped gear bonuses; the base values are the unequipped
    // baseline that everything else stacks onto.
    float playerHp           = 50.0f;
    int   playerBaseDamage   = 8;
    int   playerBaseMaxHp    = 50;
    float playerAtkRange     = 1.4f;   // tiles -- generous so iso-overshoot doesn't whiff
    float playerAtkSpeed     = 0.45f;  // seconds per swing
    int   monstersKilled     = 0;
    int   itemsPickedUp      = 0;
    bool  fogOfWar           = true;   // toggled with V; persists across scenes

    // Inventory + equipment.
    //   `equippedWeapon` / `equippedArmor` hold the active gear; `inventory`
    //   is the bag of everything else. has* flags stand in for std::optional
    //   to keep the struct trivially copyable for log dumps later.
    std::vector<Item> inventory;
    Item equippedWeapon{};
    Item equippedArmor{};
    bool hasWeapon = false;
    bool hasArmor  = false;

    int   EffectiveMaxHp() const {
        return playerBaseMaxHp + (hasArmor ? equippedArmor.maxHpBonus : 0);
    }
    int   EffectiveDamage() const {
        return playerBaseDamage + (hasWeapon ? equippedWeapon.damage : 0);
    }

    std::function<std::unique_ptr<Scene>()> makeTitle;
    std::function<std::unique_ptr<Scene>()> makeTown;
    std::function<std::unique_ptr<Scene>()> makeDungeon;

    void DrawTextCentered(const std::string& text, float y, float pixelSize,
                          const Color& color) {
        const float w = font->Measure(text, pixelSize).x;
        renderer->DrawText(*font, text, {(screenW - w) * 0.5f, y}, color, pixelSize);
    }
};

// ---------------------------------------------------------------------------
// World renderer + player logic shared by Town and Dungeon scenes.
//
// One painter-sorted draw list per frame. Subclasses inject extra actors via
// AppendActors() and HUD overlays via DrawHudExtra(); per-frame logic that
// isn't shared (combat, AI) goes in OnUpdateExtra().
// ---------------------------------------------------------------------------
struct RenderItem {
    float sortKey;
    enum class Kind { Tile, Player, Monster, GroundItem };
    Kind kind;
    int tx = 0, ty = 0;
    Tile tile = Tile::Empty;
    const Monster* monster = nullptr;
    const GroundItem* groundItem = nullptr;
};

// Per-frame markers a subclass wants on the minimap (monsters, loot, etc).
// Position is TILE-space; the minimap projects them itself.
struct MinimapDot {
    Vec2 pos{};
    Color color{};
    float size = 3.0f;
};

class WorldScene : public Scene {
public:
    WorldScene(GameContext* ctx) : m_ctx(ctx) {}

protected:
    GameContext* m_ctx = nullptr;
    Grid m_grid;
    Vec2 m_player{};        // float tile-space
    int  m_facing = 0;      // 0 S, 1 N, 2 E, 3 W
    Vec2 m_camera{};        // origin offset such that player is near screen center

    // Each subclass picks its world and what 'E' does at the special tile.
    virtual void EnterWorld() = 0;
    virtual void OnInteract() = 0;

    // Per-scene hooks. Defaults are no-ops so TownScene gets a free pass.
    virtual void OnUpdateExtra(float /*dt*/) {}
    virtual void AppendActors(std::vector<RenderItem>& /*items*/) {}
    virtual void AppendMinimapDots(std::vector<MinimapDot>& /*dots*/) {}
    virtual void DrawWorldOverlay() {}   // drawn after actors, under HUD (e.g. attack VFX)
    virtual void DrawHudExtra() {}

    // Screen-space offset applied to the player sprite this frame -- subclasses
    // use it to lunge during an attack animation. Defaults to no offset.
    virtual Vec2 PlayerDrawOffset() const { return {0.0f, 0.0f}; }

    // Whether the fog of war applies in THIS scene. Tracks the global
    // ctx->fogOfWar toggle by default; subclasses can override if a
    // scene wants to always-on or always-off the fog.
    virtual bool SceneFogOfWar() const { return m_ctx->fogOfWar; }
    // What the surrounding floor looks like -- used as an underlay so
    // wall sprites don't show void through their transparent corners.
    virtual Tile  SceneFloorKind() const { return Tile::Grass; }

    void OnEnter() override {
        EnterWorld();
        // Fog texture lives for the lifetime of this scene. It's sized to
        // gw*N x gh*N so each world tile gets a kFogSubdiv*kFogSubdiv block
        // of visibility texels -- enough resolution that the bilinear sample
        // in the fog shader makes the falloff edge look round, not blocky.
        m_fogTexW = m_grid.w * kFogSubdiv;
        m_fogTexH = m_grid.h * kFogSubdiv;
        m_fogPixels.assign(static_cast<size_t>(m_fogTexW) * m_fogTexH * 4u, 0);
        TextureDesc td;
        td.width  = static_cast<uint32_t>(m_fogTexW);
        td.height = static_cast<uint32_t>(m_fogTexH);
        td.format = TextureFormat::RGBA8;     // only .R is read by the shader
        td.filter = TextureFilter::Linear;    // bilinear smooths sub-tile edges
        td.pixels = m_fogPixels.data();
        if (m_ctx->device)
            m_fogTexture = m_ctx->device->CreateTexture(td);

        // Minimap RT cache. Allocated here, populated on the first frame
        // (m_minimapDirty defaults true) so we don't have to fight the
        // renderer's Begin/End state from inside OnEnter.
        TextureDesc mmd;
        mmd.width  = static_cast<uint32_t>(kMinimapW);
        mmd.height = static_cast<uint32_t>(kMinimapH);
        mmd.format = TextureFormat::RGBA8;
        mmd.filter = TextureFilter::Linear;
        mmd.renderTarget = true;
        if (m_ctx->device)
            m_minimapRT = m_ctx->device->CreateTexture(mmd);
        m_minimapDirty = true;

        // SceneManager runs OnUpdate then OnRender; on the FIRST frame after
        // a Change() the manager has already applied the pending push, so
        // OnEnter happens but OnUpdate for the new scene is skipped this
        // tick. That leaves m_visible empty and IsRenderItemVisible blows
        // up on the very first draw. Prime it here.
        RecomputeVisibility();
    }

    void OnUpdate(float dtRaw) override {
        const float dt = Min(dtRaw, 1.0f / 30.0f);
        if (Input::IsKeyPressed(Key::I) || Input::IsKeyPressed(Key::Tab))
            m_inventoryOpen = !m_inventoryOpen;
        if (Input::IsKeyPressed(Key::V))
            m_ctx->fogOfWar = !m_ctx->fogOfWar;
        if (Input::IsKeyPressed(Key::Escape)) {
            if (m_inventoryOpen) { m_inventoryOpen = false; return; }
            Manager().Change(m_ctx->makeTitle());
            return;
        }
        if (m_inventoryOpen) {
            // While the panel is up, freeze world updates so combat / movement
            // can't run "underneath" the UI. Mouse clicks on item slots still
            // need handling, so route those through the overlay first.
            HandleInventoryClicks();
            return;
        }

        // Input -> intent.
        const bool up    = Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::Up);
        const bool down  = Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::Down);
        const bool left  = Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::Left);
        const bool right = Input::IsKeyDown(Key::D) || Input::IsKeyDown(Key::Right);
        const bool keyboardMoving = up || down || left || right;

        // Left-click sets a tile-space move target by inverting the iso
        // projection. The same transform fog UV uses:
        //   tx = (sx - cam.x)/kTileW + (sy - cam.y)/kTileH
        //   ty = (sy - cam.y)/kTileH - (sx - cam.x)/kTileW
        // Right-click clears any pending click target. Keyboard input wins:
        // the moment WASD is pressed the click target is forgotten so the
        // two control schemes don't fight over the same frame's movement.
        if (Input::IsMousePressed(MouseButton::Left)) {
            const Vec2 mouse = Input::MousePosition();
            // Undo the world-pass zoom: pixels around screen centre map 1:1
            // until we scale back to the underlying world coords. Then run
            // the same iso inverse fog UV uses.
            const float worldSx = m_ctx->screenW * 0.5f
                                + (mouse.x - m_ctx->screenW * 0.5f) / kWorldZoom;
            const float worldSy = m_ctx->screenH * 0.5f
                                + (mouse.y - m_ctx->screenH * 0.5f) / kWorldZoom;
            const float u = (worldSx - m_camera.x) / kTileW;
            const float v = (worldSy - m_camera.y) / kTileH;
            m_clickTarget    = { u + v, v - u };
            m_hasClickTarget = true;
            m_clickMarkerTtl = 0.4f;
            // Face the click immediately so the player turns even on the
            // first frame after the click.
            const float fdx = m_clickTarget.x - m_player.x;
            const float fdy = m_clickTarget.y - m_player.y;
            if (std::fabs(fdx) > std::fabs(fdy))
                m_facing = (fdx > 0.0f) ? 2 : 3;
            else
                m_facing = (fdy > 0.0f) ? 0 : 1;
        }
        if (Input::IsMousePressed(MouseButton::Right) || keyboardMoving)
            m_hasClickTarget = false;

        m_clickMarkerTtl = std::max(0.0f, m_clickMarkerTtl - dt);

        // Map W/A/S/D to iso cardinal directions: W=N (-x,-y), S=S (+x,+y),
        // A=W (-x,+y), D=E (+x,-y). Diagonals combine, normalised. When the
        // keyboard is idle but a click target is live, fall through to the
        // (target - player) direction instead.
        float dx = 0.0f, dy = 0.0f;
        if (up)    { dx -= 1.0f; dy -= 1.0f; }
        if (down)  { dx += 1.0f; dy += 1.0f; }
        if (left)  { dx -= 1.0f; dy += 1.0f; }
        if (right) { dx += 1.0f; dy -= 1.0f; }
        if (!keyboardMoving && m_hasClickTarget) {
            const float tdx = m_clickTarget.x - m_player.x;
            const float tdy = m_clickTarget.y - m_player.y;
            const float td  = std::sqrt(tdx * tdx + tdy * tdy);
            constexpr float kArriveDist = 0.18f;
            if (td <= kArriveDist) {
                m_hasClickTarget = false;
            } else {
                dx = tdx; dy = tdy;
            }
        }
        const float mag = std::sqrt(dx * dx + dy * dy);
        if (mag > 0.0001f) {
            dx /= mag; dy /= mag;
            // Pick facing by dominant axis.
            if (std::fabs(dx) > std::fabs(dy))
                m_facing = (dx > 0.0f) ? 2 : 3;   // E / W
            else
                m_facing = (dy > 0.0f) ? 0 : 1;   // S / N
        }
        // Clamp dt to guard against pause/hitch spikes teleporting the
        // player across tiles.
        const float dtClamped = std::min(dt, 1.0f / 30.0f);
        const float stepX = dx * kPlayerSpeed * dtClamped;
        const float stepY = dy * kPlayerSpeed * dtClamped;

        // Axis-separated tile-blocked collision. kPad keeps the player
        // half-radius from the tile boundary so they don't visibly clip
        // into the wall sprite's iso footprint.
        const float kPad = 0.20f;
        const Vec2 startPos = m_player;
        const float testX = m_player.x + stepX + (stepX > 0 ? kPad : -kPad);
        if (!IsBlockedAt(testX, m_player.y)) m_player.x += stepX;
        const float testY = m_player.y + stepY + (stepY > 0 ? kPad : -kPad);
        if (!IsBlockedAt(m_player.x, testY)) m_player.y += stepY;

        // Pillar avoidance for click-to-move: if we wanted to move this
        // frame but the collision pinned us in place, the target is on the
        // far side of a wall/pillar. Try a perpendicular strafe -- both
        // axes simultaneously, since iso-cardinal targets (pure +x or +y)
        // produce zero step on one axis and the regular axis-separated
        // slide can't shake them loose. Whichever side ends up closer to
        // the click target wins; if neither perpendicular is open, the
        // player is truly boxed in and the target is dropped.
        const bool didntMove = mag > 0.0001f
            && std::fabs(m_player.x - startPos.x) < 1e-5f
            && std::fabs(m_player.y - startPos.y) < 1e-5f;
        if (m_hasClickTarget && !keyboardMoving && didntMove) {
            const float lx = -dy, ly = dx;   // 90 deg left of target heading
            const float rx =  dy, ry = -dx;  // 90 deg right
            const float probe = kPlayerSpeed * dtClamped;
            auto canSlide = [&](float fdx, float fdy) {
                const float tx = m_player.x + fdx + (fdx > 0 ? kPad : -kPad);
                const float ty = m_player.y + fdy + (fdy > 0 ? kPad : -kPad);
                return !IsBlockedAt(tx, m_player.y)
                    || !IsBlockedAt(m_player.x, ty);
            };
            const bool canL = canSlide(lx * probe, ly * probe);
            const bool canR = canSlide(rx * probe, ry * probe);
            float chosenDx = 0.0f, chosenDy = 0.0f;
            if (canL && !canR) { chosenDx = lx; chosenDy = ly; }
            else if (canR && !canL) { chosenDx = rx; chosenDy = ry; }
            else if (canL && canR) {
                // Both open -- pick the side that brings us nearer the target.
                const float dxL = m_clickTarget.x - (m_player.x + lx);
                const float dyL = m_clickTarget.y - (m_player.y + ly);
                const float dxR = m_clickTarget.x - (m_player.x + rx);
                const float dyR = m_clickTarget.y - (m_player.y + ry);
                const float distL = dxL * dxL + dyL * dyL;
                const float distR = dxR * dxR + dyR * dyR;
                if (distL <= distR) { chosenDx = lx; chosenDy = ly; }
                else                { chosenDx = rx; chosenDy = ry; }
            } else {
                m_hasClickTarget = false;
            }
            if (chosenDx != 0.0f || chosenDy != 0.0f) {
                const float sx = chosenDx * kPlayerSpeed * dtClamped;
                const float sy = chosenDy * kPlayerSpeed * dtClamped;
                const float ttx = m_player.x + sx + (sx > 0 ? kPad : -kPad);
                if (!IsBlockedAt(ttx, m_player.y)) m_player.x += sx;
                const float tty = m_player.y + sy + (sy > 0 ? kPad : -kPad);
                if (!IsBlockedAt(m_player.x, tty)) m_player.y += sy;
            }
        }

        // Auto-interact: walking onto the portal (in town) or the stairs
        // (in dungeon) triggers OnInteract() automatically -- no key press.
        // Round to the nearest tile so movement that ends slightly past the
        // cell centre still counts as "on it"; the single-tile check keeps
        // the behaviour predictable (no accidental enter from one tile
        // away). OnInteract changes scene, so return immediately after.
        {
            const int px = static_cast<int>(std::lround(m_player.x));
            const int py = static_cast<int>(std::lround(m_player.y));
            const Tile pt = m_grid.At(px, py);
            if (pt == Tile::Portal || pt == Tile::Stairs) {
                OnInteract();
                return;
            }
        }

        OnUpdateExtra(dt);

        RecomputeVisibility();
    }

    // Tile-based collision. Walls fill the whole tile, so a test point
    // inside a wall tile is blocked outright.
    bool IsBlockedAt(float ftx, float fty) const {
        const int tx = static_cast<int>(std::floor(ftx));
        const int ty = static_cast<int>(std::floor(fty));
        return TileBlocksMovement(m_grid.At(tx, ty));
    }

    // Bresenham-style LOS: walk a straight line from the player's tile to the
    // target. If any cell along the way (not counting the endpoints) is a
    // Wall, the target is hidden. Used by the fog overlay to decide which
    // tiles get blacked out.
    bool HasLineOfSight(int x0, int y0, int x1, int y1) const {
        int dx = std::abs(x1 - x0);
        int dy = -std::abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
            if (x0 == x1 && y0 == y1) return true;
            if (TileBlocksSight(m_grid.At(x0, y0))) return false;
        }
    }

    // Per-tile light level in [0, 1]. 1 = fully lit, 0 = pitch black; the
    // band in between gets a soft fog where sprites tint darker the further
    // they are from the player but are still readable. Tiles with no line
    // of sight stay at 0. Toggle off -> everything = 1.
    void RecomputeVisibility() {
        const int gw = m_grid.w, gh = m_grid.h;
        m_lightLevel.assign(static_cast<size_t>(gw) * gh, 0.0f);
        if (!SceneFogOfWar()) {
            std::fill(m_lightLevel.begin(), m_lightLevel.end(), 1.0f);
            return;
        }
        constexpr float kInnerR = 6.0f;  // < this distance = full bright
        constexpr float kOuterR = 9.0f;  // > this distance = fully dark
        const int px = (int)std::lround(m_player.x);
        const int py = (int)std::lround(m_player.y);
        const int rInt = static_cast<int>(kOuterR);
        const int x0 = std::max(0, px - rInt);
        const int x1 = std::min(gw - 1, px + rInt);
        const int y0 = std::max(0, py - rInt);
        const int y1 = std::min(gh - 1, py + rInt);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float ddx = static_cast<float>(x - px);
                const float ddy = static_cast<float>(y - py);
                const float d = std::sqrt(ddx * ddx + ddy * ddy);
                if (d > kOuterR) continue;
                if ((x != px || y != py) && !HasLineOfSight(px, py, x, y))
                    continue;
                float light = 1.0f;
                if (d > kInnerR)
                    light = (kOuterR - d) / (kOuterR - kInnerR);
                m_lightLevel[static_cast<size_t>(y) * gw + x] = light;
            }
        }

        // Wall extension: a wall adjacent to ANY visible cell becomes
        // visible too. The chain has to iterate -- a perimeter corner like
        // town's NW tile (0, 0) has no floor neighbour at all (both
        // cardinal sides are wall, the rest is off-map), so it can only
        // light up after the wall NEXT to it has been lit by the same
        // pass. Without this loop, LOS stops at the first wall and the
        // rest of the connected perimeter stays dark. We iterate over the
        // FULL grid (not just the fog box) because chain targets like
        // (0, 0) sit outside the box, and cap iterations at 32 as a
        // safety stop for runaway loops.
        for (int iter = 0; iter < 32; ++iter) {
            bool anyChanged = false;
            const std::vector<float> snapshot = m_lightLevel;
            for (int y = 0; y < gh; ++y) {
                for (int x = 0; x < gw; ++x) {
                    if (snapshot[static_cast<size_t>(y) * gw + x] > 0.0f) continue;
                    if (m_grid.At(x, y) != Tile::Wall) continue;
                    float best = 0.0f;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            const int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;
                            if (m_grid.At(nx, ny) == Tile::Empty) continue;
                            best = std::max(best,
                                snapshot[static_cast<size_t>(ny) * gw + nx]);
                        }
                    }
                    if (best > 0.0f) {
                        m_lightLevel[static_cast<size_t>(y) * gw + x] = best;
                        anyChanged = true;
                    }
                }
            }
            if (!anyChanged) break;
        }

        BuildFogPixels();
    }

    // DDA grid raycast: returns true if a straight ray from world-space
    // (px, py) reaches (wx, wy) without crossing any sight-blocking tile.
    // The destination's OWN tile doesn't count -- a ray that ends exactly on
    // a wall tile sees that wall (its front face), it just can't see past it.
    //
    // Why DDA instead of Bresenham: we sample at sub-cell precision (fog
    // texels per tile), so the start/end points are fractional. DDA steps
    // exactly along the ray to each tile boundary, giving precise wall-edge
    // shadows; Bresenham snaps to integer cells and would leak light around
    // corners or pinch shadows depending on which neighbour it picked.
    bool RayHasLOS(float px, float py, float wx, float wy) const {
        const int endTx = static_cast<int>(std::floor(wx));
        const int endTy = static_cast<int>(std::floor(wy));
        int cx = static_cast<int>(std::floor(px));
        int cy = static_cast<int>(std::floor(py));
        if (cx == endTx && cy == endTy) return true;

        const float dx = wx - px;
        const float dy = wy - py;
        const int   stepX = (dx > 0.0f) ? 1 : (dx < 0.0f ? -1 : 0);
        const int   stepY = (dy > 0.0f) ? 1 : (dy < 0.0f ? -1 : 0);

        constexpr float kBig = 1.0e30f;
        // tMax: parametric distance along the ray to the next tile boundary
        // on each axis. tDelta: distance between successive boundaries.
        float tMaxX, tMaxY, tDeltaX, tDeltaY;
        if (stepX > 0) {
            tMaxX   = ((cx + 1) - px) / dx;
            tDeltaX = 1.0f / dx;
        } else if (stepX < 0) {
            tMaxX   = (cx - px) / dx;          // dx is negative -> positive
            tDeltaX = -1.0f / dx;
        } else {
            tMaxX = tDeltaX = kBig;
        }
        if (stepY > 0) {
            tMaxY   = ((cy + 1) - py) / dy;
            tDeltaY = 1.0f / dy;
        } else if (stepY < 0) {
            tMaxY   = (cy - py) / dy;
            tDeltaY = -1.0f / dy;
        } else {
            tMaxY = tDeltaY = kBig;
        }

        // Cap iterations to the Manhattan distance plus a safety margin.
        // Each step crosses exactly one tile boundary so the bound is tight.
        const int maxIter = std::abs(endTx - cx) + std::abs(endTy - cy) + 4;
        for (int iter = 0; iter < maxIter; ++iter) {
            if (tMaxX < tMaxY) {
                cx += stepX;
                tMaxX += tDeltaX;
            } else {
                cy += stepY;
                tMaxY += tDeltaY;
            }
            if (cx == endTx && cy == endTy) return true;
            if (cx < 0 || cy < 0 || cx >= m_grid.w || cy >= m_grid.h) return false;
            if (TileBlocksSight(m_grid.At(cx, cy))) return false;
        }
        return false;
    }

    // Rebuild the sub-tile fog texture from the player position. Every sub-
    // cell gets its visibility from a per-pixel raycast against the wall
    // grid, so walls cast pixel-accurate shadows: corners reveal exactly
    // what's geometrically reachable, and the shadow line behind a wall
    // tile lands on a clean diagonal instead of a tile-aligned step. The
    // bilinear sample in the fog shader then softens that one-texel edge
    // into a smooth gradient on screen.
    void BuildFogPixels() {
        if (!m_fogTexture) return;
        const int W = m_fogTexW, H = m_fogTexH;
        if (W <= 0 || H <= 0) return;

        if (!SceneFogOfWar()) {
            // Fog off: full visibility everywhere. We still keep the texture
            // up to date so a mid-scene V toggle ON picks up correct data on
            // the very next frame without an extra OnUpdate round-trip.
            std::fill(m_fogPixels.begin(), m_fogPixels.end(), uint8_t{0xFF});
            m_fogTexture->UpdatePixels(m_fogPixels.data());
            return;
        }

        constexpr float kInnerR = 4.0f;
        constexpr float kOuterR = 9.0f;
        const float invBand = 1.0f / (kOuterR - kInnerR);
        const float invN    = 1.0f / static_cast<float>(kFogSubdiv);
        const float px = m_player.x;
        const float py = m_player.y;
        const int   gw = m_grid.w;

        // Only sub-cells within kOuterR of the player can ever be lit. Clear
        // the whole texture first, then raycast just the window around the
        // player. On a 60x60 town the full 480x480 sub-cell raycast is ~2M
        // op/frame; this keeps it under a tenth of that without touching
        // visible fog quality.
        std::fill(m_fogPixels.begin(), m_fogPixels.end(), uint8_t{0});
        const int subPx = static_cast<int>(std::floor(px * kFogSubdiv));
        const int subPy = static_cast<int>(std::floor(py * kFogSubdiv));
        const int subR  = static_cast<int>(std::ceil(kOuterR * kFogSubdiv)) + 1;
        const int sxStart = std::max(0, subPx - subR);
        const int syStart = std::max(0, subPy - subR);
        const int sxEnd   = std::min(W - 1, subPx + subR);
        const int syEnd   = std::min(H - 1, subPy + subR);

        for (int sy = syStart; sy <= syEnd; ++sy) {
            const int ty = sy / kFogSubdiv;
            const float wy = (sy + 0.5f) * invN;
            const float ddy = wy - py;
            for (int sx = sxStart; sx <= sxEnd; ++sx) {
                const int tx = sx / kFogSubdiv;
                const float wx = (sx + 0.5f) * invN;
                const float ddx = wx - px;
                const float d = std::sqrt(ddx * ddx + ddy * ddy);

                float light = 0.0f;
                if (d < kOuterR) {
                    // Wall sub-cells use tile-level visibility (LOS + chain
                    // extension) instead of a strict sub-cell raycast: a
                    // connected wall row inevitably has its inner neighbours
                    // sitting on the path from the player to its outer ones,
                    // and sub-cell raycasting them blocks each other in a
                    // stripey pattern. Tile-level vis treats the wall row as
                    // one surface, then the sub-cell distance falloff still
                    // gives a smooth gradient across the wall sprite.
                    const bool visible =
                        TileBlocksSight(m_grid.At(tx, ty))
                            ? m_lightLevel[static_cast<size_t>(ty) * gw + tx] > 0.0f
                            : RayHasLOS(px, py, wx, wy);
                    if (visible) {
                        if (d <= kInnerR) light = 1.0f;
                        else              light = (kOuterR - d) * invBand;
                    }
                }
                const uint8_t L8 = static_cast<uint8_t>(
                    std::clamp(light * 255.0f, 0.0f, 255.0f));
                const size_t idx = (static_cast<size_t>(sy) * W + sx) * 4u;
                m_fogPixels[idx + 0] = L8;  // R: visibility multiplier
                m_fogPixels[idx + 1] = L8;  // G/B/A mirror R so a debug dump
                m_fogPixels[idx + 2] = L8;  // of the atlas reads as grayscale
                m_fogPixels[idx + 3] = 0xFF;
            }
        }
        m_fogTexture->UpdatePixels(m_fogPixels.data());
    }

    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;

        // Camera: origin places the player's screen position at the centre.
        const Vec2 playerScreen0 = TileToScreen(m_player.x, m_player.y, {0.0f, 0.0f});
        m_camera = { m_ctx->screenW * 0.5f - playerScreen0.x,
                     m_ctx->screenH * 0.5f - playerScreen0.y };

        // World pass uses a zoomed ortho so tiles + characters read bigger
        // without changing any sprite sizes. The visible window shrinks to
        // (screenW/zoom) x (screenH/zoom), centred on the screen midpoint,
        // and that region gets stretched to fill the backbuffer. m_camera
        // is unchanged -- it still maps the player to screen centre, which
        // is also the ortho's centre, so the player stays pinned in the
        // middle of the view at the new zoom level.
        const float vw = m_ctx->screenW / kWorldZoom;
        const float vh = m_ctx->screenH / kWorldZoom;
        const float cxz = m_ctx->screenW * 0.5f;
        const float cyz = m_ctx->screenH * 0.5f;
        r.Begin(mat4::Ortho(cxz - vw * 0.5f, cxz + vw * 0.5f,
                            cyz + vh * 0.5f, cyz - vh * 0.5f,
                            -1.0f, 1.0f));

        // Engine doesn't clear the backbuffer in BeginFrame, so paint a full
        // background here. Without this, leftover pixels from the prior scene
        // (most visibly the title text) bleed through wherever the iso grid
        // doesn't cover the screen. With fog enabled the background also
        // serves as the "out of sight" colour -- hidden tiles aren't drawn
        // and just show through to whatever's behind, so we want that to
        // be pure void rather than a coloured tint.
        r.DrawQuad({0.0f, 0.0f}, {m_ctx->screenW, m_ctx->screenH},
                   Color(0.0f, 0.0f, 0.0f, 1.0f));

        // Switch to fog mode for the world pass. SetFog uploads a 2x3 affine
        // transform that maps each vertex's SCREEN coordinates back to fog
        // texture UV (= world tile coords / grid dims). The shader does the
        // bilinear sample and multiplies it into the sprite, giving smooth
        // pixel-level fade across tile boundaries. We pop fog before HUD so
        // text / minimap / inventory render at full brightness.
        const bool useFog = SceneFogOfWar() && m_fogTexture != nullptr;
        m_fogActive = useFog;
        if (useFog) {
            const float gw = static_cast<float>(m_grid.w);
            const float gh = static_cast<float>(m_grid.h);
            // TileToScreen(tx, ty) = ((tx-ty)*kHalfTileW + cam.x, (tx+ty)*kHalfTileH + cam.y).
            // Inverting: tx = (sx-cam.x)/kTileW + (sy-cam.y)/kTileH
            //            ty = (sy-cam.y)/kTileH - (sx-cam.x)/kTileW
            // Then fog UV = (tx/gw, ty/gh).
            const float au = 1.0f / (kTileW * gw);
            const float bu = 1.0f / (kTileH * gw);
            const float av = -1.0f / (kTileW * gh);
            const float bv = 1.0f / (kTileH * gh);
            m_fogURow = Vec4{au, bu, -(au * m_camera.x + bu * m_camera.y), 0.0f};
            m_fogVRow = Vec4{av, bv, -(av * m_camera.x + bv * m_camera.y), 0.0f};
            r.SetFog(m_fogTexture.get(), m_fogURow, m_fogVRow);
        }

        // Two-pass painter: floors first (the ground plane lies under
        // everything), then everything else (walls, actors, loot, portal
        // glow) sorted by iso depth. A single mixed pass biased floors
        // down by a small epsilon, but the player's float (px + py) could
        // still land BELOW the integer (a + b) of a nearby floor tile in
        // front of them -- that floor would then draw on top of the
        // figure and clip the character's feet.
        for (int y = 0; y < m_grid.h; ++y) {
            for (int x = 0; x < m_grid.w; ++x) {
                const Tile t = m_grid.At(x, y);
                if (t == Tile::Empty) continue;
                // No tile-level cull: the fog shader multiplies each pixel by
                // its sub-cell visibility, so tiles outside the radius render
                // as pure black (= invisible against the background). Letting
                // them draw means the outer cutoff curves through tiles
                // instead of stepping at tile borders.
                if (!TileIsTall(t)) {
                    DrawTile(x, y, t);
                } else if (t == Tile::Wall) {
                    // Wall underlay: paint the scene's floor under every
                    // wall tile here in the floor pass so adjacent walls
                    // don't drop their neighbour's body when a later
                    // wall's underlay would otherwise overdraw it
                    // mid-sort.
                    DrawWallUnderlay(x, y);
                }
            }
        }

        std::vector<RenderItem> items;
        items.reserve(m_grid.w * m_grid.h + 4);
        for (int y = 0; y < m_grid.h; ++y) {
            for (int x = 0; x < m_grid.w; ++x) {
                const Tile t = m_grid.At(x, y);
                if (t == Tile::Empty || !TileIsTall(t)) continue;
                RenderItem it;
                it.sortKey = static_cast<float>(x + y);
                it.kind = RenderItem::Kind::Tile;
                it.tx = x; it.ty = y; it.tile = t;
                items.push_back(it);
            }
        }
        {
            RenderItem it;
            it.sortKey = m_player.x + m_player.y;
            it.kind = RenderItem::Kind::Player;
            items.push_back(it);
        }
        AppendActors(items);
        std::sort(items.begin(), items.end(),
                  [](const RenderItem& a, const RenderItem& b) { return a.sortKey < b.sortKey; });

        for (const RenderItem& it : items) {
            if (!IsRenderItemVisible(it)) continue;
            switch (it.kind) {
                case RenderItem::Kind::Tile:       DrawTile(it.tx, it.ty, it.tile); break;
                case RenderItem::Kind::Player:     DrawPlayer();                    break;
                case RenderItem::Kind::Monster:    DrawMonster(*it.monster);        break;
                case RenderItem::Kind::GroundItem: DrawGroundItem(*it.groundItem);  break;
            }
        }

        DrawWorldOverlay();

        // Click-to-move target marker -- a small fading diamond on the tile
        // the player is walking toward. Pulses for the first 0.4s after the
        // click (m_clickMarkerTtl), then stays as a faint waypoint until
        // arrival so the player can see where they're headed.
        if (m_hasClickTarget) {
            const Vec2 marker = TileToScreen(m_clickTarget.x, m_clickTarget.y, m_camera);
            const float pulse = (m_clickMarkerTtl > 0.0f) ? (m_clickMarkerTtl / 0.4f) : 0.0f;
            const float baseAlpha = 0.35f + 0.45f * pulse;
            const float size = 6.0f + 8.0f * pulse;
            // Outer halo
            r.DrawQuad({marker.x - size, marker.y - size * 0.5f},
                       {size * 2.0f, size},
                       Color(1.0f, 0.95f, 0.55f, baseAlpha * 0.5f));
            // Inner diamond core
            r.DrawQuad({marker.x - 3.0f, marker.y - 1.5f}, {6.0f, 3.0f},
                       Color(1.0f, 1.0f, 0.85f, baseAlpha));
        }

        // Leave fog mode so HUD / inventory / minimap render at full
        // brightness regardless of the player's visibility state.
        if (useFog)
            r.SetFog(nullptr, {}, {});

        // End the zoomed world pass and start a 1:1 HUD pass. Otherwise the
        // HUD text, minimap, and inventory panel would inherit the world
        // zoom and either oversize the screen or overflow the safe area.
        r.End();
        r.Begin(Renderer2D::ScreenCamera(m_ctx->screenW, m_ctx->screenH));

        // HUD.
        r.DrawText(*m_ctx->font, HudLine(), {16.0f, 12.0f}, Color::White(), 20.0f);
        DrawHudExtra();
        DrawMinimap();

        // Inventory panel overlay last so it sits above all gameplay UI.
        DrawInventoryOverlay();

        r.End();
    }

    void DrawTile(int tx, int ty, Tile t) {
        Renderer2D& r = *m_ctx->renderer;
        const Vec2 center = TileToScreen((float)tx, (float)ty, m_camera);

        // TileToScreen(tx, ty) returns the NORTH corner of the tile diamond
        // on screen (since world point (tx, ty) is the tile's NW vertex).
        // The diamond's centre is one halfH south of that, and its south
        // corner is one full tile height south of N.
        //
        // Floor sprites are 64x32 with the diamond centred at the sprite's
        // own centre, so the sprite's top-left lands at (center.x - halfW,
        // center.y) for the diamond to align with the tile.
        //
        // Visibility tinting (the per-tile L/WallL multiplier we used to
        // bake into every sprite color) now comes from the fog shader, which
        // bilinear-samples the visibility texture per pixel. Quad tints stay
        // White() so they don't darken twice.
        auto drawFloor = [&](const TextureRegion& region) {
            const Vec2 pos{center.x - kHalfTileW, center.y};
            r.DrawQuad(pos, {kTileW, kFloorTileH}, region, Color::White());
        };
        auto drawTall = [&](const TextureRegion& region,
                            const Color& tint = Color::White()) {
            // Tall sprites (64x96) anchor their bottom at the floor's SOUTH
            // corner -- screen y = center.y + tile height. Sprite top-left
            // is then at y = center.y + tile height - sprite height.
            const Vec2 pos{center.x - kHalfTileW,
                           center.y + kTileH - kWallSpriteH};
            r.DrawQuad(pos, {kTileW, kWallSpriteH}, region, tint);
        };

        switch (t) {
            case Tile::Floor:  drawFloor(m_ctx->sprites->Get("floorStone")); break;
            case Tile::Grass:  drawFloor(m_ctx->sprites->Get("floorGrass")); break;
            case Tile::Dirt:   drawFloor(m_ctx->sprites->Get("floorDirt"));  break;
            case Tile::Stairs: drawFloor(m_ctx->sprites->Get("stairs"));     break;
            case Tile::Wall: {
                // The floor underlay for walls is painted in the floor
                // pass (see DrawWallUnderlay) so later walls' underlays
                // don't overwrite earlier walls' bodies. Here we only
                // draw the wall sprite itself.
                //
                // Walls have to come out of fog mode for the draw. A wall
                // sprite is 96px tall and paints UP over the tile NORTH of
                // its own; with per-pixel fog that top half samples the
                // northern tile's visibility, so a wall next to a shadowed
                // floor shows a ghosted shadow on its cap. Switching to
                // sprite mode and baking the wall's chain-extended light
                // into the tint keeps the whole sprite at one brightness --
                // no ghost seam where the wall meets the floor behind it.
                const float wallLight = WallLight(tx, ty);
                Color tint = WallOcclusionTint(tx, ty);
                tint.r *= wallLight;
                tint.g *= wallLight;
                tint.b *= wallLight;
                if (m_fogActive) r.SetFog(nullptr, {}, {});
                drawTall(m_ctx->sprites->Get("wall"), tint);
                if (m_fogActive) r.SetFog(m_fogTexture.get(), m_fogURow, m_fogVRow);
                break;
            }
            case Tile::Portal:
                // Portal floats above a floor tile -- draw stone first, then the
                // glow over it so the portal looks "set into" the ground.
                drawFloor(m_ctx->sprites->Get("floorStone"));
                drawTall (m_ctx->sprites->Get("portal"));
                break;
            case Tile::Empty: break;
        }
    }

    // Pick the floor sprite that should appear UNDER the wall so the room's
    // ground plane reads continuously. Sample cardinal floor neighbours; the
    void DrawWallUnderlay(int tx, int ty) {
        Renderer2D& r = *m_ctx->renderer;
        const Vec2 center = TileToScreen((float)tx, (float)ty, m_camera);
        const char* name = (SceneFloorKind() == Tile::Grass)
            ? "floorGrass" : "floorStone";
        // Tint is White() -- the fog shader handles visibility per pixel.
        r.DrawQuad({center.x - kHalfTileW, center.y},
                   {kTileW, kFloorTileH},
                   m_ctx->sprites->Get(name),
                   Color::White());
    }

    // dominant tile type wins.
    // Walls that visually sit between the camera and the player turn semi-
    // transparent so the figure stays readable -- the standard Diablo trick.
    // The occlusion zone is a diamond 0..3 iso tiles "forward" of the player
    // (sum > player sum) and within roughly one tile of the player's iso
    // column. Inside that zone we return a 40% alpha tint; otherwise white.
    Color WallOcclusionTint(int tx, int ty) const {
        // Fade walls at or in front of the player's iso column so a wall
        // standing in the player's render layer doesn't bury the figure.
        // fwd >= 0 catches the sort-key ties at corners too (wall (1, 19)
        // and player at (1.5, 18.5) both sum to 20.0 -- the wall would
        // otherwise paint over the player half the time).
        //
        // Alpha is kept low (0.25) so when several occluding walls overlap,
        // the cap/south/east face boundaries don't pile up into visible
        // seams between adjacent transparent walls. The original 0.50 made
        // each wall's faces tint the next one's, leaving a sharp colour
        // discontinuity along every interior wall edge.
        const float dx = static_cast<float>(tx) - m_player.x;
        const float dy = static_cast<float>(ty) - m_player.y;
        const float fwd  = dx + dy;
        const float side = dx - dy;
        if (fwd >= 0.0f && fwd < 3.5f && std::fabs(side) < 1.6f)
            return Color(1.0f, 1.0f, 1.0f, 0.25f);
        return Color::White();
    }

    void DrawPlayer() {
        Renderer2D& r = *m_ctx->renderer;
        const Vec2 center = TileToScreen(m_player.x, m_player.y, m_camera);
        const char* names[4] = {"playerS", "playerN", "playerE", "playerW"};
        const TextureRegion& reg = m_ctx->sprites->Get(names[m_facing]);
        // Anchor the sprite's bottom at the tile's floor centre, i.e. the
        // exact screen point TileToScreen(player.x, player.y) returns.
        const Vec2 offset = PlayerDrawOffset();
        const Vec2 pos{center.x - kPlayerSpriteW * 0.5f + offset.x,
                       center.y - kPlayerSpriteH       + offset.y};
        const Color tint = (m_ctx->playerHp < m_ctx->EffectiveMaxHp() && m_playerHurtFlash > 0.0f)
            ? Color(1.0f, 0.4f, 0.4f, 1.0f)
            : Color::White();
        // Drop out of fog mode for the player draw -- same fix as walls.
        // A 32x48 sprite paints up over the tile NORTH of the player, and
        // per-pixel fog there samples that other tile's visibility, which
        // is darker (further from the player or raycast-occluded) and
        // leaks through as a shadow on the player's head/torso. The
        // player is always at the centre of visibility anyway, so a flat
        // tint reads as "always lit" without any shadow seam at the feet.
        if (m_fogActive) r.SetFog(nullptr, {}, {});
        r.DrawQuad(pos, {kPlayerSpriteW, kPlayerSpriteH}, reg, tint);
        if (m_fogActive) r.SetFog(m_fogTexture.get(), m_fogURow, m_fogVRow);
    }

    void DrawMonster(const Monster& m) {
        if (!m.alive) return;
        Renderer2D& r = *m_ctx->renderer;
        const Vec2 center = TileToScreen(m.pos.x, m.pos.y, m_camera);
        const char* name = (m.kind == Monster::Kind::Skeleton) ? "skeleton" : "zombie";
        const TextureRegion& reg = m_ctx->sprites->Get(name);
        const Vec2 pos{center.x - kMonsterSpriteW * 0.5f,
                       center.y - kMonsterSpriteH};
        // Fog shader applies the visibility multiplier per pixel; the tint
        // here only carries hit-flash brightening (or White() otherwise).
        const Color tint = (m.hurtFlashTimer > 0.0f)
            ? Color(1.6f, 1.6f, 1.6f, 1.0f)
            : Color::White();
        r.DrawQuad(pos, {kMonsterSpriteW, kMonsterSpriteH}, reg, tint);

        // HP bar appears only on damaged monsters so a full-strength room
        // doesn't look like a UI dump.
        if (m.hp < m.maxHp) {
            const float barW = 26.0f;
            const float barH = 4.0f;
            const Vec2 barPos{center.x - barW * 0.5f, center.y - kMonsterSpriteH - 6.0f};
            r.DrawQuad(barPos, {barW, barH}, Color(0.15f, 0.05f, 0.05f, 0.85f));
            r.DrawQuad({barPos.x + 1.0f, barPos.y + 1.0f},
                       {(barW - 2.0f) * std::max(0.0f, m.hp / m.maxHp), barH - 2.0f},
                       Color(0.85f, 0.20f, 0.20f, 1.0f));
        }
    }

    void DrawGroundItem(const GroundItem& gi) {
        Renderer2D& r = *m_ctx->renderer;
        const Vec2 center = TileToScreen(gi.pos.x, gi.pos.y, m_camera);
        const float bob = std::sin(gi.bobPhase) * 2.0f;
        // Fog shader applies the visibility multiplier per pixel.
        const Color core = RarityColor(gi.item.rarity);
        // Outer halo first (larger, transparent), inner gem on top -- reads
        // as a glowing pickup against the floor without needing a sprite.
        r.DrawQuad({center.x - 14.0f, center.y - 12.0f + bob}, {28.0f, 18.0f},
                   Color(core.r, core.g, core.b, 0.35f));
        r.DrawQuad({center.x - 8.0f,  center.y - 8.0f  + bob}, {16.0f, 10.0f},
                   core);
        // Tiny dark outline cap on top so the gem reads with depth even
        // against a bright floor.
        r.DrawQuad({center.x - 8.0f, center.y - 8.0f + bob}, {16.0f, 2.0f},
                   Color(core.r * 0.3f, core.g * 0.3f, core.b * 0.3f, 0.85f));
    }

    // ----- Inventory panel ---------------------------------------------------
    static constexpr int kInventoryCap = 12;
    static constexpr float kPanelW    = 480.0f;
    static constexpr float kPanelH    = 520.0f;
    static constexpr float kEqSlotW   = 220.0f;
    static constexpr float kEqSlotH   = 60.0f;
    static constexpr float kBagSlotW  = 220.0f;
    static constexpr float kBagSlotH  = 50.0f;
    static constexpr float kBagGapX   = 10.0f;
    static constexpr float kBagGapY   = 6.0f;

    Vec2 PanelOrigin() const {
        return { (m_ctx->screenW - kPanelW) * 0.5f,
                 (m_ctx->screenH - kPanelH) * 0.5f };
    }
    Vec2 WeaponSlotOrigin() const {
        const Vec2 p = PanelOrigin();
        return { p.x + 16.0f, p.y + 80.0f };
    }
    Vec2 ArmorSlotOrigin() const {
        const Vec2 p = PanelOrigin();
        return { p.x + 16.0f + kEqSlotW + 10.0f, p.y + 80.0f };
    }
    Vec2 BagSlotOrigin(int idx) const {
        const Vec2 p = PanelOrigin();
        const int col = idx % 2;
        const int row = idx / 2;
        return { p.x + 16.0f + col * (kBagSlotW + kBagGapX),
                 p.y + 240.0f + row * (kBagSlotH + kBagGapY) };
    }
    static bool HitRect(Vec2 p, Vec2 origin, float w, float h) {
        return p.x >= origin.x && p.x < origin.x + w
            && p.y >= origin.y && p.y < origin.y + h;
    }

    void HandleInventoryClicks() {
        if (!Input::IsMousePressed(MouseButton::Left)) return;
        const Vec2 mp = Input::MousePosition();
        if (HitRect(mp, WeaponSlotOrigin(), kEqSlotW, kEqSlotH)) {
            if (m_ctx->hasWeapon &&
                (int)m_ctx->inventory.size() < kInventoryCap) {
                m_ctx->inventory.push_back(m_ctx->equippedWeapon);
                m_ctx->hasWeapon = false;
            }
            return;
        }
        if (HitRect(mp, ArmorSlotOrigin(), kEqSlotW, kEqSlotH)) {
            if (m_ctx->hasArmor &&
                (int)m_ctx->inventory.size() < kInventoryCap) {
                m_ctx->inventory.push_back(m_ctx->equippedArmor);
                m_ctx->hasArmor = false;
            }
            return;
        }
        for (int i = 0; i < (int)m_ctx->inventory.size() && i < kInventoryCap; ++i) {
            if (HitRect(mp, BagSlotOrigin(i), kBagSlotW, kBagSlotH)) {
                UseOrEquipBagItem(i);
                return;
            }
        }
    }

    void UseOrEquipBagItem(int idx) {
        if (idx < 0 || idx >= (int)m_ctx->inventory.size()) return;
        Item it = m_ctx->inventory[idx];
        m_ctx->inventory.erase(m_ctx->inventory.begin() + idx);
        switch (it.kind) {
            case ItemKind::Potion: {
                const float maxHp = static_cast<float>(m_ctx->EffectiveMaxHp());
                m_ctx->playerHp = std::min(maxHp, m_ctx->playerHp + it.healAmount);
                break;
            }
            case ItemKind::Sword:
                if (m_ctx->hasWeapon) m_ctx->inventory.push_back(m_ctx->equippedWeapon);
                m_ctx->equippedWeapon = it;
                m_ctx->hasWeapon = true;
                break;
            case ItemKind::Armor:
                if (m_ctx->hasArmor)  m_ctx->inventory.push_back(m_ctx->equippedArmor);
                m_ctx->equippedArmor = it;
                m_ctx->hasArmor = true;
                break;
        }
    }

    void DrawEquippedSlot(Vec2 origin, bool has, const Item& item, const char* label) {
        Renderer2D& r = *m_ctx->renderer;
        const Color slotBg = has ? RarityColor(item.rarity)
                                 : Color(0.45f, 0.40f, 0.40f, 1.0f);
        r.DrawQuad(origin, {kEqSlotW, kEqSlotH},
                   Color(slotBg.r * 0.30f, slotBg.g * 0.30f, slotBg.b * 0.30f, 0.85f));
        r.DrawText(*m_ctx->font, label, {origin.x + 8.0f, origin.y + 6.0f},
                   Color(0.65f, 0.60f, 0.55f, 1.0f), 12.0f);
        if (has) {
            r.DrawText(*m_ctx->font, item.name,
                       {origin.x + 8.0f, origin.y + 22.0f},
                       RarityColor(item.rarity), 14.0f);
            char stat[48];
            if (item.kind == ItemKind::Sword)
                std::snprintf(stat, sizeof(stat), "+%d damage", item.damage);
            else
                std::snprintf(stat, sizeof(stat), "+%d max HP", item.maxHpBonus);
            r.DrawText(*m_ctx->font, stat,
                       {origin.x + 8.0f, origin.y + 40.0f},
                       Color(0.85f, 0.85f, 0.85f, 1.0f), 12.0f);
        } else {
            r.DrawText(*m_ctx->font, "(empty -- click bag item to equip)",
                       {origin.x + 8.0f, origin.y + 30.0f},
                       Color(0.55f, 0.50f, 0.45f, 1.0f), 12.0f);
        }
    }

    void DrawBagSlot(Vec2 origin, const Item& item) {
        Renderer2D& r = *m_ctx->renderer;
        const Color bg = RarityColor(item.rarity);
        r.DrawQuad(origin, {kBagSlotW, kBagSlotH},
                   Color(bg.r * 0.30f, bg.g * 0.30f, bg.b * 0.30f, 0.85f));
        r.DrawText(*m_ctx->font, item.name,
                   {origin.x + 8.0f, origin.y + 5.0f},
                   RarityColor(item.rarity), 13.0f);
        char stat[48];
        if (item.kind == ItemKind::Sword)
            std::snprintf(stat, sizeof(stat), "+%d damage  (%s sword)",
                          item.damage, RarityName(item.rarity));
        else if (item.kind == ItemKind::Armor)
            std::snprintf(stat, sizeof(stat), "+%d max HP  (%s armor)",
                          item.maxHpBonus, RarityName(item.rarity));
        else
            std::snprintf(stat, sizeof(stat), "heals %d HP  (use)", item.healAmount);
        r.DrawText(*m_ctx->font, stat,
                   {origin.x + 8.0f, origin.y + 26.0f},
                   Color(0.80f, 0.80f, 0.80f, 1.0f), 12.0f);
    }

    // ----- Minimap (top-right corner) ---------------------------------------
    // Iso-aligned -- tiles project the same way they do in the world view,
    // so "up-right on screen" reads as the same direction on both. unit is
    // adaptive: the iso bbox of (gridW + gridH) tiles is fitted into the
    // panel with a small inset, so a large dungeon and a small town both
    // fill the panel sensibly.
    // unit = pixels per tile width. Iso bbox: (W+H)*unit/2 wide and
    // (W+H)*unit/4 tall. Solve for the tighter of the two axes.
    float MinimapUnit() const {
        const float side = static_cast<float>(m_grid.w + m_grid.h);
        const float availW = kMinimapW - 2.0f * kMinimapInset;
        const float availH = kMinimapH - 2.0f * kMinimapInset;
        return std::min(2.0f * availW / side, 4.0f * availH / side);
    }

    void DrawMinimap() {
        if (m_grid.w <= 0 || m_grid.h <= 0) return;
        Renderer2D& r = *m_ctx->renderer;
        constexpr float kMargin = 16.0f;
        const Vec2 panel{m_ctx->screenW - kMinimapW - kMargin, kMargin + 40.0f};

        // First time we render this scene, rasterise the static grid into
        // the RT. From then on the panel is a single textured quad and we
        // only re-issue the dynamic dots + player marker each frame.
        if (m_minimapRT && m_minimapDirty) {
            r.SetRenderTarget(m_minimapRT.get());
            r.Clear(Color(0.0f, 0.0f, 0.0f, 0.72f));

            // In RT-local space the origin is just (panel-x = 0).
            const float unit = MinimapUnit();
            const float originX = kMinimapW * 0.5f;
            const float cellW = std::max(unit, 1.5f);
            const float cellH = std::max(unit * 0.5f, 1.0f);
            auto toMMLocal = [&](float tx, float ty) -> Vec2 {
                return { originX + (tx - ty) * unit * 0.5f,
                         kMinimapInset + (tx + ty) * unit * 0.25f };
            };

            for (int y = 0; y < m_grid.h; ++y) {
                for (int x = 0; x < m_grid.w; ++x) {
                    const Tile t = m_grid.At(x, y);
                    if (t == Tile::Empty) continue;
                    Color c;
                    switch (t) {
                        case Tile::Floor:  c = Color(0.32f, 0.30f, 0.28f, 1.0f); break;
                        case Tile::Grass:  c = Color(0.28f, 0.50f, 0.24f, 1.0f); break;
                        case Tile::Dirt:   c = Color(0.55f, 0.42f, 0.28f, 1.0f); break;
                        case Tile::Wall:   c = Color(0.70f, 0.68f, 0.64f, 1.0f); break;
                        case Tile::Portal: c = Color(0.30f, 0.65f, 1.00f, 1.0f); break;
                        case Tile::Stairs: c = Color(1.00f, 0.85f, 0.30f, 1.0f); break;
                        default: continue;
                    }
                    const Vec2 mm = toMMLocal(static_cast<float>(x),
                                              static_cast<float>(y));
                    r.DrawQuad({mm.x - cellW * 0.5f, mm.y - cellH * 0.5f},
                               {cellW, cellH}, c);
                }
            }
            r.SetRenderTarget(nullptr);
            m_minimapDirty = false;
        }

        // Sample the cached grid into the screen-space panel. One textured
        // quad replaces the thousands of tile draws the old path issued.
        if (m_minimapRT) {
            r.DrawQuad(panel, {kMinimapW, kMinimapH},
                       m_minimapRT.get(), Color::White());
        } else {
            // Plain backing if RT alloc failed.
            r.DrawQuad(panel, {kMinimapW, kMinimapH},
                       Color(0.0f, 0.0f, 0.0f, 0.72f));
        }

        // Live overlay (screen-space): monster / item dots + player marker.
        const float unit = MinimapUnit();
        const float originX = panel.x + kMinimapW * 0.5f;
        const float originY = panel.y + kMinimapInset;
        auto toMM = [&](float tx, float ty) -> Vec2 {
            return { originX + (tx - ty) * unit * 0.5f,
                     originY + (tx + ty) * unit * 0.25f };
        };

        std::vector<MinimapDot> dots;
        AppendMinimapDots(dots);
        for (const MinimapDot& d : dots) {
            const Vec2 mm = toMM(d.pos.x, d.pos.y);
            r.DrawQuad({mm.x - d.size * 0.5f, mm.y - d.size * 0.5f},
                       {d.size, d.size}, d.color);
        }

        // Player marker is drawn last so it can never be hidden under a
        // monster or item dot that happens to overlap the same minimap pixel.
        const Vec2 pmm = toMM(m_player.x, m_player.y);
        r.DrawQuad({pmm.x - 3.0f, pmm.y - 3.0f}, {6.0f, 6.0f},
                   Color(0.0f, 0.0f, 0.0f, 0.9f));
        r.DrawQuad({pmm.x - 2.0f, pmm.y - 2.0f}, {4.0f, 4.0f},
                   Color(0.95f, 0.95f, 1.00f, 1.0f));
    }

    void DrawInventoryOverlay() {
        if (!m_inventoryOpen) return;
        Renderer2D& r = *m_ctx->renderer;
        // Dim the world behind so the focus is unambiguously on the panel.
        r.DrawQuad({0.0f, 0.0f}, {m_ctx->screenW, m_ctx->screenH},
                   Color(0.0f, 0.0f, 0.0f, 0.55f));
        const Vec2 p = PanelOrigin();
        r.DrawQuad(p, {kPanelW, kPanelH}, Color(0.10f, 0.08f, 0.10f, 0.96f));
        r.DrawText(*m_ctx->font, "INVENTORY",
                   {p.x + 20.0f, p.y + 18.0f},
                   Color(0.95f, 0.85f, 0.75f, 1.0f), 26.0f);
        r.DrawText(*m_ctx->font, "EQUIPPED",
                   {p.x + 20.0f, p.y + 60.0f},
                   Color(0.7f, 0.65f, 0.6f, 1.0f), 14.0f);
        DrawEquippedSlot(WeaponSlotOrigin(), m_ctx->hasWeapon, m_ctx->equippedWeapon,
                         "WEAPON");
        DrawEquippedSlot(ArmorSlotOrigin(),  m_ctx->hasArmor,  m_ctx->equippedArmor,
                         "ARMOR");

        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "BAG  (%d / %d)",
                      (int)m_ctx->inventory.size(), kInventoryCap);
        r.DrawText(*m_ctx->font, hdr,
                   {p.x + 20.0f, p.y + 215.0f},
                   Color(0.7f, 0.65f, 0.6f, 1.0f), 14.0f);
        for (int i = 0; i < (int)m_ctx->inventory.size() && i < kInventoryCap; ++i)
            DrawBagSlot(BagSlotOrigin(i), m_ctx->inventory[i]);

        r.DrawText(*m_ctx->font,
                   "I or Tab close  -  click bag item to equip/use  -  click equipped to unequip",
                   {p.x + 16.0f, p.y + kPanelH - 26.0f},
                   Color(0.55f, 0.5f, 0.45f, 1.0f), 12.0f);
    }

    float TileLight(int tx, int ty) const {
        if (tx < 0 || ty < 0 || tx >= m_grid.w || ty >= m_grid.h) return 0.0f;
        return m_lightLevel[static_cast<size_t>(ty) * m_grid.w + tx];
    }

    // Walls live one tile inside the floor they bound, so a wall at L=1.0
    // next to a floor at L=0.5 reads as "bright wall in fading area" which
    // breaks the fog illusion. Average the wall's tile light with its
    // 8-neighbour lit tiles so the wall slides between its surrounding
    // floors instead of standing out.
    float WallLight(int tx, int ty) const {
        const float own = TileLight(tx, ty);
        if (own <= 0.0f) return 0.0f;
        float total = own;
        int count = 1;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const float l = TileLight(tx + dx, ty + dy);
                if (l > 0.0f) { total += l; ++count; }
            }
        }
        return total / static_cast<float>(count);
    }

    bool IsRenderItemVisible(const RenderItem& it) const {
        if (!SceneFogOfWar()) return true;
        if (it.kind == RenderItem::Kind::Player) return true;
        // Walls (and portals, stairs) draw regardless of tile light: the fog
        // shader fades them per pixel, so a wall on the edge of vision shows
        // a smooth gradient across its sprite instead of vanishing whole at
        // the tile-light cutoff.
        if (it.kind == RenderItem::Kind::Tile) return true;
        int tx = 0, ty = 0;
        switch (it.kind) {
            case RenderItem::Kind::Monster:
                tx = static_cast<int>(std::lround(it.monster->pos.x));
                ty = static_cast<int>(std::lround(it.monster->pos.y));
                break;
            case RenderItem::Kind::GroundItem:
                tx = static_cast<int>(std::lround(it.groundItem->pos.x));
                ty = static_cast<int>(std::lround(it.groundItem->pos.y));
                break;
            case RenderItem::Kind::Tile:
            case RenderItem::Kind::Player:
                return true;
        }
        return TileLight(tx, ty) > 0.0f;
    }

    std::vector<float> m_lightLevel;      // recomputed every frame in OnUpdate
    bool  m_inventoryOpen   = false;
    float m_playerHurtFlash = 0.0f;   // ticked down by combat code; gated on player tint

    // World-pass camera zoom. The ortho in OnRender shrinks the visible
    // window to (screenW/zoom) x (screenH/zoom) and stretches it across the
    // backbuffer, so sprites and tiles read bigger. Mouse-to-tile inversion
    // in OnUpdate undoes the same zoom so clicks land where the player
    // visually clicked.
    static constexpr float kWorldZoom = 1.7f;

    // Click-to-move target. Set by a left mouse press; cleared when the
    // player arrives, or when keyboard input takes over.
    Vec2  m_clickTarget    {0.0f, 0.0f};   // tile-space
    bool  m_hasClickTarget = false;
    float m_clickMarkerTtl = 0.0f;          // separate fade for the on-floor marker

    // Cached fog UV affine transform (filled in OnRender). Wall draws need to
    // temporarily flip out of fog mode and back, so we keep the params handy
    // instead of recomputing them per wall.
    Vec4 m_fogURow{};
    Vec4 m_fogVRow{};
    bool m_fogActive = false;

    // Pixel-level fog of war. The R channel is the visibility multiplier in
    // [0, 1] sampled by the fog shader (engine/shaders/sprite_fog.frag). We
    // subdivide each world tile into kFogSubdiv*kFogSubdiv texels so the
    // distance-falloff edge curves smoothly through tiles instead of stepping
    // at tile boundaries. RecomputeVisibility() rebuilds it every player tile
    // move; the GPU bilinear-samples it via SetFog in OnRender.
    //
    // 8 texels per tile means the smallest visible "step" on screen is ~8
    // pixels for floor tiles (64-wide divided across 8 sub-cells), and the
    // GPU bilinearly interpolates between neighbours so even those 8-pixel
    // blocks blend smoothly. 4 was too coarse -- the eye picked up the sub-
    // cell grid as a separate stepping artifact.
    static constexpr int kFogSubdiv = 8;
    std::unique_ptr<RHITexture> m_fogTexture;
    std::vector<uint8_t>        m_fogPixels;
    int  m_fogTexW = 0;
    int  m_fogTexH = 0;

    // Minimap render-target cache. The grid (tiles) is static for the life
    // of a scene, so we rasterise it once into m_minimapRT and just sample
    // it every frame. Monsters, ground items, and the player marker still
    // draw live over the cached panel so dynamic info stays current. Skips
    // ~3600 quad calls per frame on the 60x60 town.
    static constexpr float kMinimapW = 200.0f;
    static constexpr float kMinimapH = 160.0f;
    static constexpr float kMinimapInset = 6.0f;
    std::unique_ptr<RHITexture> m_minimapRT;
    bool m_minimapDirty = true;

    virtual std::string HudLine() const = 0;
};

// ---------------------------------------------------------------------------
// Town and Dungeon as thin subclasses of WorldScene.
// ---------------------------------------------------------------------------
class TownScene : public WorldScene {
public:
    TownScene(GameContext* ctx) : WorldScene(ctx) {}

protected:
    void EnterWorld() override {
        TownResult t = BuildTown();
        m_grid = std::move(t.grid);
        m_player = t.playerSpawn;
        m_facing = 1;  // facing north toward the portal
        // Town heals you to full -- Diablo-style safe haven. Without this a
        // crawl that ends at 5/50 HP gives you no real reason to ever come
        // back; full heal is the standard contract.
        m_ctx->playerHp = static_cast<float>(m_ctx->EffectiveMaxHp());
    }
    void OnInteract() override {
        ++m_ctx->dungeonVisits;
        m_ctx->rngSeed = m_ctx->rngSeed * 1103515245u + 12345u;
        Manager().Change(m_ctx->makeDungeon());
    }
    std::string HudLine() const override {
        char buf[240];
        std::snprintf(buf, sizeof(buf),
                      "TOWN  -  WASD walk  -  V fog %s  -  visits %d  bag %d  -  xy %.2f,%.2f",
                      m_ctx->fogOfWar ? "on" : "off",
                      m_ctx->dungeonVisits, (int)m_ctx->inventory.size(),
                      m_player.x, m_player.y);
        return buf;
    }
};

class DungeonScene : public WorldScene {
public:
    DungeonScene(GameContext* ctx) : WorldScene(ctx) {}

    Tile SceneFloorKind() const override { return Tile::Floor; }

protected:
    static constexpr float kMonsterAggroRange = 6.5f;
    static constexpr float kMonsterMeleeRange = 0.85f;
    static constexpr float kMonsterAtkSpeed   = 1.2f;
    static constexpr float kSlashDuration     = 0.16f;
    static constexpr float kHurtFlashDuration = 0.2f;

    void EnterWorld() override {
        DungeonGenResult g = GenerateDungeon(m_ctx->rngSeed);
        m_grid   = std::move(g.grid);
        m_player = g.playerSpawn;
        m_facing = 0;
        m_attackCd = 0.0f;
        m_slashTimer = 0.0f;
        m_groundItems.clear();
        m_dropRng.seed(m_ctx->rngSeed ^ 0xC0FFEE05u);

        // Spawn monsters: pick floor cells from g.monsterSpawns at random,
        // alternating skeleton / zombie. Density scales with how many
        // candidate cells there are -- big rooms get more monsters.
        std::mt19937 rng(m_ctx->rngSeed ^ 0xA1B2C3D4u);
        std::shuffle(g.monsterSpawns.begin(), g.monsterSpawns.end(), rng);
        const int count = std::min<int>(
            static_cast<int>(g.monsterSpawns.size()),
            8 + static_cast<int>(g.monsterSpawns.size() / 20));
        m_monsters.clear();
        m_monsters.reserve(count);
        for (int i = 0; i < count; ++i) {
            const Monster::Kind kind = (i % 2 == 0)
                ? Monster::Kind::Skeleton : Monster::Kind::Zombie;
            m_monsters.push_back(Monster::Make(kind, g.monsterSpawns[i]));
        }
    }

    void OnInteract() override { Manager().Change(m_ctx->makeTown()); }

    void OnUpdateExtra(float dt) override {
        // Timers first so a 0-duration cooldown this frame still gates
        // a fresh attack.
        m_attackCd        = std::max(0.0f, m_attackCd - dt);
        m_slashTimer      = std::max(0.0f, m_slashTimer - dt);
        m_playerHurtFlash = std::max(0.0f, m_playerHurtFlash - dt);
        for (Monster& m : m_monsters) {
            if (!m.alive) continue;
            m.attackCooldown = std::max(0.0f, m.attackCooldown - dt);
            m.hurtFlashTimer = std::max(0.0f, m.hurtFlashTimer - dt);
        }

        // Player attack -- Space. Hit center is one facing-step in front of
        // the player; any live monster inside playerAtkRange takes damage.
        if ((Input::IsKeyPressed(Key::Space) ||
             Input::IsKeyPressed(Key::J)) && m_attackCd <= 0.0f) {
            m_attackCd   = m_ctx->playerAtkSpeed;
            m_slashTimer = kSlashDuration;
            const Vec2 fd = FacingDir(m_facing);
            const Vec2 hit = { m_player.x + fd.x, m_player.y + fd.y };
            for (Monster& m : m_monsters) {
                if (!m.alive) continue;
                const float dx = m.pos.x - hit.x;
                const float dy = m.pos.y - hit.y;
                if (dx * dx + dy * dy <= m_ctx->playerAtkRange * m_ctx->playerAtkRange) {
                    m.hp -= static_cast<float>(m_ctx->EffectiveDamage());
                    m.hurtFlashTimer = kHurtFlashDuration;
                    if (m.hp <= 0.0f) {
                        m.alive = false;
                        ++m_ctx->monstersKilled;
                        RollDrop(m.pos);
                    }
                }
            }
        }

        // Monster AI -- chase if within aggro, melee on cooldown.
        for (Monster& m : m_monsters) {
            if (!m.alive) continue;
            float dx = m_player.x - m.pos.x;
            float dy = m_player.y - m.pos.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > kMonsterAggroRange) continue;

            if (dist > kMonsterMeleeRange && dist > 0.0001f) {
                const float ux = dx / dist;
                const float uy = dy / dist;
                const float stepX = ux * m.speed * dt;
                const float stepY = uy * m.speed * dt;
                if (!IsBlockedAt(m.pos.x + stepX, m.pos.y))
                    m.pos.x += stepX;
                if (!IsBlockedAt(m.pos.x, m.pos.y + stepY))
                    m.pos.y += stepY;
            } else if (m.attackCooldown <= 0.0f) {
                m.attackCooldown = kMonsterAtkSpeed;
                m_ctx->playerHp -= m.damage;
                m_playerHurtFlash = kHurtFlashDuration;
            }
        }

        // Pickups: walk-over auto-pickup. We iterate from the back so
        // erase() during the scan doesn't shift indices we're about to
        // visit. Inventory full -> the item just stays on the ground.
        for (int i = (int)m_groundItems.size() - 1; i >= 0; --i) {
            GroundItem& gi = m_groundItems[i];
            gi.bobPhase += dt * 3.0f;
            const float dx = m_player.x - gi.pos.x;
            const float dy = m_player.y - gi.pos.y;
            if (dx * dx + dy * dy < 0.55f * 0.55f &&
                (int)m_ctx->inventory.size() < kInventoryCap) {
                m_ctx->inventory.push_back(gi.item);
                ++m_ctx->itemsPickedUp;
                m_groundItems.erase(m_groundItems.begin() + i);
            }
        }

        // Death: snap back to town with full HP. The drop loot stays on the
        // dungeon floor of THIS layout; re-entering re-rolls the dungeon so
        // they're gone anyway. v3 doesn't punish death further.
        if (m_ctx->playerHp <= 0.0f) {
            m_ctx->playerHp = static_cast<float>(m_ctx->EffectiveMaxHp());
            Manager().Change(m_ctx->makeTown());
        }
    }

    // Roll an item on monster death. ~40% nothing, ~12% potion, otherwise
    // a gear item with rarity weights baked into GenerateGearItem.
    void RollDrop(Vec2 pos) {
        std::uniform_int_distribution<int> d(0, 99);
        const int r = d(m_dropRng);
        if (r < 40) return;
        GroundItem gi;
        gi.pos = pos;
        gi.bobPhase = static_cast<float>(r) * 0.1f;  // de-sync bob between drops
        if (r < 52) gi.item = MakePotion();
        else        gi.item = GenerateGearItem(m_dropRng);
        m_groundItems.push_back(gi);
    }

    void AppendActors(std::vector<RenderItem>& items) override {
        for (const Monster& m : m_monsters) {
            if (!m.alive) continue;
            RenderItem it;
            it.sortKey = m.pos.x + m.pos.y + 0.0005f;  // tie-break above tile, below player
            it.kind = RenderItem::Kind::Monster;
            it.monster = &m;
            items.push_back(it);
        }
        for (const GroundItem& gi : m_groundItems) {
            RenderItem it;
            // Slight negative bias so ground items render BELOW the floor's
            // sort-tie but ABOVE the wall behind them. They're flat decals.
            it.sortKey = gi.pos.x + gi.pos.y + 0.0002f;
            it.kind = RenderItem::Kind::GroundItem;
            it.groundItem = &gi;
            items.push_back(it);
        }
    }

    void AppendMinimapDots(std::vector<MinimapDot>& dots) override {
        for (const Monster& m : m_monsters) {
            if (!m.alive) continue;
            dots.push_back({m.pos, Color(0.95f, 0.20f, 0.20f, 1.0f), 3.0f});
        }
        for (const GroundItem& gi : m_groundItems)
            dots.push_back({gi.pos, RarityColor(gi.item.rarity), 3.0f});
    }

    // While a slash is active, lunge the player sprite in their facing
    // direction. The bell curve (4 * t * (1-t)) peaks at the middle of the
    // animation: sprite slides forward, then retracts, which on a 0.16s
    // window reads as a clear swing.
    Vec2 PlayerDrawOffset() const override {
        if (m_slashTimer <= 0.0f) return {0.0f, 0.0f};
        const float t = 1.0f - (m_slashTimer / kSlashDuration);  // 0 -> 1
        const float bell = 4.0f * t * (1.0f - t);
        // Convert iso-tile facing to screen-space direction by projecting one
        // step in front of the player and subtracting the player's screen
        // position. That way the lunge angle matches the facing visually.
        const Vec2 fd = FacingDir(m_facing);
        const Vec2 here  = TileToScreen(m_player.x,        m_player.y,        {0,0});
        const Vec2 ahead = TileToScreen(m_player.x + fd.x, m_player.y + fd.y, {0,0});
        const float dx = ahead.x - here.x;
        const float dy = ahead.y - here.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) return {0.0f, 0.0f};
        constexpr float kMaxLunge = 8.0f; // screen pixels at peak
        return { dx / len * bell * kMaxLunge,
                 dy / len * bell * kMaxLunge };
    }

    void DrawWorldOverlay() override {
        if (m_slashTimer <= 0.0f) return;
        Renderer2D& r = *m_ctx->renderer;
        const Vec2 fd = FacingDir(m_facing);

        // Anchor the VFX one tile in front of the player. World-space so it
        // scrolls with the camera.
        const Vec2 worldHit = { m_player.x + fd.x * 0.85f,
                                m_player.y + fd.y * 0.85f };
        const Vec2 screen = TileToScreen(worldHit.x, worldHit.y, m_camera);
        const float t = 1.0f - (m_slashTimer / kSlashDuration);   // 0 -> 1

        // Facing in screen space -- the swing axis sweeps around this.
        const Vec2 here  = TileToScreen(m_player.x,        m_player.y,        {0,0});
        const Vec2 ahead = TileToScreen(m_player.x + fd.x, m_player.y + fd.y, {0,0});
        const float sxFwd = ahead.x - here.x;
        const float syFwd = ahead.y - here.y;
        const float fwdLen = std::sqrt(sxFwd * sxFwd + syFwd * syFwd);
        if (fwdLen < 0.001f) return;
        const float fwdAngle = std::atan2(syFwd, sxFwd);
        constexpr float kArcSpan = 1.6f;   // radians, ~90 degrees total

        // (1) Wide soft glow at the swing's current position -- gives the
        // arc some volume against the floor.
        {
            const float glowAngle = fwdAngle + (t - 0.5f) * kArcSpan;
            const float glowSize = 26.0f + 8.0f * (1.0f - std::fabs(t - 0.5f) * 2.0f);
            const Vec2 glowCenter{
                screen.x + std::cos(glowAngle) * 8.0f,
                screen.y + std::sin(glowAngle) * 8.0f,
            };
            const float glowAlpha = 0.35f * (1.0f - std::fabs(t - 0.5f) * 1.6f);
            if (glowAlpha > 0.0f) {
                r.DrawQuad(glowCenter, {glowSize * 1.4f, glowSize}, glowAngle,
                           Color(1.0f, 0.75f, 0.30f, glowAlpha));
            }
        }

        // (2) Motion-blurred trail of streaks. Each iteration places a streak
        // at an earlier point in the swing's arc, fading and shrinking with
        // age. Together they read as a curved blade trail rather than a
        // single rotated quad.
        constexpr int kTrailCount = 6;
        for (int i = 0; i < kTrailCount; ++i) {
            const float tt = t - (i / static_cast<float>(kTrailCount)) * 0.55f;
            if (tt < 0.0f || tt > 1.0f) continue;
            const float angle = fwdAngle + (tt - 0.5f) * kArcSpan;
            const float fade  = (1.0f - tt) * (1.0f - i / static_cast<float>(kTrailCount));
            const float lenScale = 1.0f - 0.10f * i;
            const float thkScale = 1.0f - 0.13f * i;
            const float streakLen = 42.0f * lenScale;
            const float streakThk = 8.0f  * thkScale;
            const Vec2 streakCenter{
                screen.x + std::cos(angle) * 10.0f,
                screen.y + std::sin(angle) * 10.0f,
            };
            // Warm outer streak
            r.DrawQuad(streakCenter, {streakLen, streakThk}, angle,
                       Color(1.00f, 0.78f, 0.30f, fade * 0.70f));
            // Bright inner core
            r.DrawQuad(streakCenter, {streakLen * 0.65f, streakThk * 0.35f}, angle,
                       Color(1.00f, 0.98f, 0.85f, fade * 0.95f));
        }

        // (3) Impact flash in the first ~30% of the animation. A bright burst
        // at the swing's centre that quickly contracts -- sells the "hit" feel
        // even when nothing is in melee range.
        if (t < 0.35f) {
            const float fk = 1.0f - (t / 0.35f);
            const float flashSize = 28.0f * fk + 6.0f;
            r.DrawQuad({screen.x - flashSize * 0.5f, screen.y - flashSize * 0.25f},
                       {flashSize, flashSize * 0.5f},
                       Color(1.0f, 0.95f, 0.7f, 0.55f * fk));
            // Tight white pop in the very first frames
            if (t < 0.12f) {
                const float pop = 1.0f - (t / 0.12f);
                const float pSize = 14.0f * pop + 4.0f;
                r.DrawQuad({screen.x - pSize * 0.5f, screen.y - pSize * 0.5f},
                           {pSize, pSize},
                           Color(1.0f, 1.0f, 1.0f, 0.85f * pop));
            }
        }

        // (4) Sparks scattered along the leading edge of the swing. Each one
        // uses a hash of the spark index for stable per-attack jitter so the
        // pattern looks intentional but isn't perfectly symmetrical.
        constexpr int kSparkCount = 8;
        for (int i = 0; i < kSparkCount; ++i) {
            // Cheap deterministic jitter: distinct angle + distance per spark
            const float jAngle = ((i * 1327) % 100) / 100.0f - 0.5f;       // [-0.5, 0.5]
            const float jDist  = ((i *  733) % 100) / 100.0f;              // [0, 1]
            const float spAngle = fwdAngle + (t - 0.5f) * kArcSpan
                                + jAngle * 0.6f;
            const float spDist  = 14.0f + jDist * 22.0f;
            const float spX = screen.x + std::cos(spAngle) * spDist;
            const float spY = screen.y + std::sin(spAngle) * spDist;
            // Sparks fade over the slash lifetime; the further-out ones
            // appear later in the swing to suggest particles flung outward.
            const float spawnT = jDist * 0.4f;          // 0..0.4
            if (t < spawnT) continue;
            const float age = (t - spawnT) / std::max(0.001f, 1.0f - spawnT);
            const float sparkAlpha = (1.0f - age) * 0.95f;
            const float sparkSize = 2.5f - age * 1.5f;
            if (sparkSize <= 0.0f) continue;
            r.DrawQuad({spX - sparkSize, spY - sparkSize},
                       {sparkSize * 2.0f, sparkSize * 2.0f},
                       Color(1.0f, 0.95f, 0.65f, sparkAlpha));
        }
    }

    void DrawHudExtra() override {
        Renderer2D& r = *m_ctx->renderer;
        // Player HP bar bottom-left: dark backing + red foreground.
        const float barX = 16.0f;
        const float barY = m_ctx->screenH - 40.0f;
        const float barW = 240.0f;
        const float barH = 18.0f;
        r.DrawQuad({barX, barY}, {barW, barH}, Color(0.12f, 0.08f, 0.08f, 0.85f));
        const float maxHp = static_cast<float>(m_ctx->EffectiveMaxHp());
        const float fillW = (barW - 4.0f) * std::max(0.0f, m_ctx->playerHp / maxHp);
        r.DrawQuad({barX + 2.0f, barY + 2.0f}, {fillW, barH - 4.0f},
                   Color(0.80f, 0.18f, 0.18f, 1.0f));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "HP  %d / %d",
                      (int)std::ceil(m_ctx->playerHp), m_ctx->EffectiveMaxHp());
        r.DrawText(*m_ctx->font, buf, {barX + 8.0f, barY - 22.0f},
                   Color(0.95f, 0.90f, 0.85f, 1.0f), 18.0f);
    }

    std::string HudLine() const override {
        char buf[240];
        std::snprintf(buf, sizeof(buf),
                      "DUNGEON  -  WASD/Space  -  walk onto stairs  -  I inventory  -  V fog %s  -  kills %d  items %d",
                      m_ctx->fogOfWar ? "on" : "off",
                      m_ctx->monstersKilled, m_ctx->itemsPickedUp);
        return buf;
    }

private:
    // Iso-cardinal facing vector in tile-space, length ~1. Diagonals on
    // screen, axis-aligned in world.
    static Vec2 FacingDir(int facing) {
        switch (facing) {
            case 0: return { 0.707f,  0.707f}; // S
            case 1: return {-0.707f, -0.707f}; // N
            case 2: return { 0.707f, -0.707f}; // E
            case 3: return {-0.707f,  0.707f}; // W
        }
        return {0.0f, 1.0f};
    }

    std::vector<Monster>     m_monsters;
    std::vector<GroundItem>  m_groundItems;
    std::mt19937             m_dropRng;
    float m_attackCd   = 0.0f;
    float m_slashTimer = 0.0f;
};

// ---------------------------------------------------------------------------
// Title scene.
// ---------------------------------------------------------------------------
class TitleScene : public Scene {
public:
    TitleScene(GameContext* ctx) : m_ctx(ctx) {}

    void OnUpdate(float) override {
        if (Input::IsKeyPressed(Key::Enter) || Input::IsKeyPressed(Key::Space))
            Manager().Change(m_ctx->makeTown());
        if (Input::IsKeyPressed(Key::Escape))
            Application::Get().Close();
    }

    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;
        r.Begin(Renderer2D::ScreenCamera(m_ctx->screenW, m_ctx->screenH));
        r.DrawQuad({0, 0}, {m_ctx->screenW, m_ctx->screenH},
                   Color(0.08f, 0.06f, 0.10f, 1.0f));
        m_ctx->DrawTextCentered("DIABLO  -  Nara",        180.0f, 64.0f,
                                Color(0.95f, 0.30f, 0.15f, 1.0f));
        m_ctx->DrawTextCentered("press ENTER to begin",   280.0f, 28.0f,
                                Color(0.85f, 0.80f, 0.70f, 1.0f));
        m_ctx->DrawTextCentered("WASD move  -  Space attack  -  Esc quit",
                                420.0f, 22.0f,
                                Color(0.7f, 0.65f, 0.55f, 1.0f));
        r.End();
    }

private:
    GameContext* m_ctx;
};

} // namespace

// ---------------------------------------------------------------------------
// App.
// ---------------------------------------------------------------------------
class DiabloApp : public Application {
public:
    DiabloApp() : Application(MakeConfig()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;
        m_spritesTexture = LoadTexture(GetDevice(), assets + "/sprites.png",
                                       TextureFilter::Point);
        m_font = LoadMSDFFont(GetDevice(), assets + "/msdf-inter.json");

        m_sprites = TextureAtlas(m_spritesTexture.get());
        // Row 0 (y=0..31): floors + stairs, 64x32 each
        m_sprites.Add("floorStone", 0,   0, 64, 32);
        m_sprites.Add("floorGrass", 64,  0, 64, 32);
        m_sprites.Add("floorDirt",  128, 0, 64, 32);
        m_sprites.Add("stairs",     192, 0, 64, 32);
        // Row 1 (y=32..127): tall things, 64x96 each. One opaque wall
        // sprite per tile -- no orientation variants, no half-arm
        // overlays. Walls are solid tiles in tile-space too, so the
        // sprite just has to fill the diamond and the lit cap on top.
        m_sprites.Add("wall",       0,   32, 64, 96);
        m_sprites.Add("portal",     64,  32, 64, 96);
        // Row 2 (y=128..175): player, 32x48 each. Sprite art (front face vs
        // back-of-head vs side profile) carries the facing cue instead of
        // a larger frame, so the figure matches the monster scale.
        m_sprites.Add("playerS",    0,   128, 32, 48);
        m_sprites.Add("playerN",    32,  128, 32, 48);
        m_sprites.Add("playerE",    64,  128, 32, 48);
        m_sprites.Add("playerW",    96,  128, 32, 48);
        // Row 3 (y=176..223): monsters, 32x48 each
        m_sprites.Add("skeleton",   0,   176, 32, 48);
        m_sprites.Add("zombie",     32,  176, 32, 48);

        m_ctx.renderer = &m_renderer;
        m_ctx.device   = &GetDevice();
        m_ctx.font     = &m_font;
        m_ctx.sprites  = &m_sprites;
        m_ctx.makeTitle   = [this]() { return std::make_unique<TitleScene>(&m_ctx); };
        m_ctx.makeTown    = [this]() { return std::make_unique<TownScene>(&m_ctx); };
        m_ctx.makeDungeon = [this]() { return std::make_unique<DungeonScene>(&m_ctx); };

        // Initial seed has SOMETHING non-trivial so the first dungeon isn't the
        // same layout for every player. Date/Math.random are unavailable to the
        // workflow harness but fine in normal app startup -- here we just take
        // a stir of the address space.
        m_ctx.rngSeed = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&m_ctx) ^ 0x9E3779B9u);

        m_scenes.Change(m_ctx.makeTitle());
        NARAGAME_INFO("Diablo: scaffold (title -> town -> dungeon, iso flat floors)");
    }

    void OnStop() override {
        m_spritesTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override { m_scenes.Update(dt); }
    void OnRender() override { m_scenes.Render(); }

private:
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title = "NaraGame - Diablo";
        c.width = static_cast<uint32_t>(kScreenW);
        c.height = static_cast<uint32_t>(kScreenH);
        return c;
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_spritesTexture;
    MSDFFont m_font;
    TextureAtlas m_sprites;
    GameContext m_ctx;
    SceneManager m_scenes;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new DiabloApp();
}
