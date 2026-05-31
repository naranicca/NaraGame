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
constexpr float kPlayerSpriteW = 32.0f;
constexpr float kPlayerSpriteH = 48.0f;

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
    Empty   = 0,  // off-map / hole. Not drawn.
    Floor   = 1,  // dungeon stone
    Grass   = 2,  // town
    Dirt    = 3,  // town path
    Wall    = 4,  // blocks movement, tall sprite
    Portal  = 5,  // interact -> enter dungeon
    Stairs  = 6,  // interact -> return to town
};

bool TileBlocksMovement(Tile t) {
    return t == Tile::Wall || t == Tile::Empty;
}

bool TileIsTall(Tile t) {
    return t == Tile::Wall || t == Tile::Portal;
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

    // Connect rooms in order with L-shaped corridors (1 tile wide).
    auto CarveLineX = [&](int y, int x0, int x1) {
        if (x0 > x1) std::swap(x0, x1);
        for (int x = x0; x <= x1; ++x) r.grid.Set(x, y, Tile::Floor);
    };
    auto CarveLineY = [&](int x, int y0, int y1) {
        if (y0 > y1) std::swap(y0, y1);
        for (int y = y0; y <= y1; ++y) r.grid.Set(x, y, Tile::Floor);
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
    const int gw = 20, gh = 20;
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
    // A short dirt path from the south wall up toward the portal.
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

    void OnEnter() override {
        EnterWorld();
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

        // Map W/A/S/D to iso cardinal directions: W=N (-x,-y), S=S (+x,+y),
        // A=W (-x,+y), D=E (+x,-y). Diagonals combine, normalised.
        float dx = 0.0f, dy = 0.0f;
        if (up)    { dx -= 1.0f; dy -= 1.0f; }
        if (down)  { dx += 1.0f; dy += 1.0f; }
        if (left)  { dx -= 1.0f; dy += 1.0f; }
        if (right) { dx += 1.0f; dy -= 1.0f; }
        const float mag = std::sqrt(dx * dx + dy * dy);
        if (mag > 0.0001f) {
            dx /= mag; dy /= mag;
            // Pick facing by dominant axis.
            if (std::fabs(dx) > std::fabs(dy))
                m_facing = (dx > 0.0f) ? 2 : 3;   // E / W
            else
                m_facing = (dy > 0.0f) ? 0 : 1;   // S / N
        }
        const float stepX = dx * kPlayerSpeed * dt;
        const float stepY = dy * kPlayerSpeed * dt;

        // Axis-separated collision: try X then Y so sliding along a wall works.
        auto blocked = [&](float ftx, float fty) {
            const int tx = (int)std::floor(ftx);
            const int ty = (int)std::floor(fty);
            return TileBlocksMovement(m_grid.At(tx, ty));
        };
        const float kPad = 0.25f;
        const float testX = m_player.x + stepX + (stepX > 0 ? kPad : -kPad);
        if (!blocked(testX, m_player.y)) m_player.x += stepX;
        const float testY = m_player.y + stepY + (stepY > 0 ? kPad : -kPad);
        if (!blocked(m_player.x, testY)) m_player.y += stepY;

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
            if (m_grid.At(x0, y0) == Tile::Wall) return false;
        }
    }

    // Recompute the visibility grid each frame. Tiles inside a fixed radius
    // and with an unblocked line of sight from the player are visible; the
    // rest stay false and get painted over with black in DrawFog().
    void RecomputeVisibility() {
        const int gw = m_grid.w, gh = m_grid.h;
        m_visible.assign(static_cast<size_t>(gw) * gh, false);
        if (!m_ctx->fogOfWar) {
            std::fill(m_visible.begin(), m_visible.end(), true);
            return;
        }
        constexpr int kRadius = 9;
        const int px = (int)std::lround(m_player.x);
        const int py = (int)std::lround(m_player.y);
        const int x0 = std::max(0, px - kRadius);
        const int x1 = std::min(gw - 1, px + kRadius);
        const int y0 = std::max(0, py - kRadius);
        const int y1 = std::min(gh - 1, py + kRadius);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const int ddx = x - px, ddy = y - py;
                if (ddx * ddx + ddy * ddy > kRadius * kRadius) continue;
                if ((x == px && y == py) || HasLineOfSight(px, py, x, y))
                    m_visible[static_cast<size_t>(y) * gw + x] = true;
            }
        }
    }

    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;

        // Camera: origin places the player's screen position at the centre.
        const Vec2 playerScreen0 = TileToScreen(m_player.x, m_player.y, {0.0f, 0.0f});
        m_camera = { m_ctx->screenW * 0.5f - playerScreen0.x,
                     m_ctx->screenH * 0.5f - playerScreen0.y };

        r.Begin(Renderer2D::ScreenCamera(m_ctx->screenW, m_ctx->screenH));

        // Engine doesn't clear the backbuffer in BeginFrame, so paint a full
        // background here. Without this, leftover pixels from the prior scene
        // (most visibly the title text) bleed through wherever the iso grid
        // doesn't cover the screen. With fog enabled the background also
        // serves as the "out of sight" colour -- hidden tiles aren't drawn
        // and just show through to whatever's behind, so we want that to
        // be pure void rather than a coloured tint.
        r.DrawQuad({0.0f, 0.0f}, {m_ctx->screenW, m_ctx->screenH},
                   Color(0.0f, 0.0f, 0.0f, 1.0f));

        // Draw tiles + actors in painter order (back-to-front = ascending
        // (tx + ty)). Floor tiles bias their sortKey down a hair so the
        // player and wall sprites at the same logical (x+y) draw on top of
        // the floor underneath them.
        std::vector<RenderItem> items;
        items.reserve(m_grid.w * m_grid.h + 4);
        for (int y = 0; y < m_grid.h; ++y) {
            for (int x = 0; x < m_grid.w; ++x) {
                const Tile t = m_grid.At(x, y);
                if (t == Tile::Empty) continue;
                RenderItem it;
                it.sortKey = (float)(x + y) - (TileIsTall(t) ? 0.0f : 0.001f);
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

        // Floor sprites are 64x32, anchored so the diamond centers on `center`.
        auto drawFloor = [&](const TextureRegion& region) {
            const Vec2 pos{center.x - kHalfTileW, center.y - kHalfTileH};
            r.DrawQuad(pos, {kTileW, kFloorTileH}, region);
        };
        auto drawTall = [&](const TextureRegion& region,
                            const Color& tint = Color::White()) {
            // Tall sprites (64x96) anchor with the BOTTOM of the diamond at
            // (center.x, center.y + halfH). The top of the sprite then sits
            // at y = center.y + halfH - 96.
            const Vec2 pos{center.x - kHalfTileW,
                           center.y + kHalfTileH - kWallSpriteH};
            r.DrawQuad(pos, {kTileW, kWallSpriteH}, region, tint);
        };

        switch (t) {
            case Tile::Floor:  drawFloor(m_ctx->sprites->Get("floorStone")); break;
            case Tile::Grass:  drawFloor(m_ctx->sprites->Get("floorGrass")); break;
            case Tile::Dirt:   drawFloor(m_ctx->sprites->Get("floorDirt"));  break;
            case Tile::Stairs: drawFloor(m_ctx->sprites->Get("stairs"));     break;
            case Tile::Wall:
                drawTall(m_ctx->sprites->Get("wall"), WallOcclusionTint(tx, ty));
                break;
            case Tile::Portal:
                // Portal floats above a floor tile -- draw stone first, then the
                // glow over it so the portal looks "set into" the ground.
                drawFloor(m_ctx->sprites->Get("floorStone"));
                drawTall (m_ctx->sprites->Get("portal"));
                break;
            case Tile::Empty: break;
        }
    }

    // Walls that visually sit between the camera and the player turn semi-
    // transparent so the figure stays readable -- the standard Diablo trick.
    // The occlusion zone is a diamond 0..3 iso tiles "forward" of the player
    // (sum > player sum) and within roughly one tile of the player's iso
    // column. Inside that zone we return a 40% alpha tint; otherwise white.
    Color WallOcclusionTint(int tx, int ty) const {
        const float dx = static_cast<float>(tx) - m_player.x;
        const float dy = static_cast<float>(ty) - m_player.y;
        const float fwd  = dx + dy;    // > 0 means wall is in front of player
        const float side = dx - dy;    // 0 means same iso-column as the player
        if (fwd > 0.0f && fwd < 3.5f && std::fabs(side) < 1.6f)
            return Color(1.0f, 1.0f, 1.0f, 0.40f);
        return Color::White();
    }

    void DrawPlayer() {
        Renderer2D& r = *m_ctx->renderer;
        const Vec2 center = TileToScreen(m_player.x, m_player.y, m_camera);
        const char* names[4] = {"playerS", "playerN", "playerE", "playerW"};
        const TextureRegion& reg = m_ctx->sprites->Get(names[m_facing]);
        // Anchor the player's feet at the tile center (slightly raised so the
        // sprite doesn't visually sink into the diamond).
        const Vec2 pos{center.x - kPlayerSpriteW * 0.5f,
                       center.y - kPlayerSpriteH + 4.0f};
        const Color tint = (m_ctx->playerHp < m_ctx->EffectiveMaxHp() && m_playerHurtFlash > 0.0f)
            ? Color(1.0f, 0.4f, 0.4f, 1.0f)
            : Color::White();
        r.DrawQuad(pos, {kPlayerSpriteW, kPlayerSpriteH}, reg, tint);
    }

    void DrawMonster(const Monster& m) {
        if (!m.alive) return;
        Renderer2D& r = *m_ctx->renderer;
        const Vec2 center = TileToScreen(m.pos.x, m.pos.y, m_camera);
        const char* name = (m.kind == Monster::Kind::Skeleton) ? "skeleton" : "zombie";
        const TextureRegion& reg = m_ctx->sprites->Get(name);
        const Vec2 pos{center.x - kPlayerSpriteW * 0.5f,
                       center.y - kPlayerSpriteH + 4.0f};
        const Color tint = (m.hurtFlashTimer > 0.0f)
            ? Color(1.6f, 1.6f, 1.6f, 1.0f)   // overbrighten -> looks like a hit flash
            : Color::White();
        r.DrawQuad(pos, {kPlayerSpriteW, kPlayerSpriteH}, reg, tint);

        // HP bar appears only on damaged monsters so a full-strength room
        // doesn't look like a UI dump.
        if (m.hp < m.maxHp) {
            const float barW = 26.0f;
            const float barH = 4.0f;
            const Vec2 barPos{center.x - barW * 0.5f, center.y - kPlayerSpriteH - 6.0f};
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
    void DrawMinimap() {
        if (m_grid.w <= 0 || m_grid.h <= 0) return;
        Renderer2D& r = *m_ctx->renderer;
        constexpr float kPanelW = 200.0f;
        constexpr float kPanelH = 160.0f;
        constexpr float kMargin = 16.0f;
        constexpr float kInset  = 6.0f;
        const Vec2 panel{m_ctx->screenW - kPanelW - kMargin, kMargin + 40.0f};

        r.DrawQuad(panel, {kPanelW, kPanelH}, Color(0.0f, 0.0f, 0.0f, 0.72f));

        // unit = pixels per tile width. Iso bbox: (W+H)*unit/2 wide and
        // (W+H)*unit/4 tall. Solve for the tighter of the two axes.
        const float side = static_cast<float>(m_grid.w + m_grid.h);
        const float availW = kPanelW - 2.0f * kInset;
        const float availH = kPanelH - 2.0f * kInset;
        const float unit = std::min(2.0f * availW / side, 4.0f * availH / side);
        const Vec2 origin{panel.x + kPanelW * 0.5f,
                          panel.y + kPanelH * 0.5f - (side * 0.25f * unit) * 0.5f
                                  + (side * 0.25f * unit) * 0.5f};
        // The two halves of `origin.y` look like a no-op but they read as
        // "centre vertically, then offset by half the iso height upward so
        // the diamond is centred not anchored top". Leaving them explicit
        // makes the math obvious if/when the panel dimensions change.

        auto toMM = [&](float tx, float ty) -> Vec2 {
            return { origin.x + (tx - ty) * unit * 0.5f,
                     panel.y + kInset + (tx + ty) * unit * 0.25f };
        };

        const float cellW = std::max(unit, 1.5f);
        const float cellH = std::max(unit * 0.5f, 1.0f);

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
                const Vec2 mm = toMM(static_cast<float>(x), static_cast<float>(y));
                r.DrawQuad({mm.x - cellW * 0.5f, mm.y - cellH * 0.5f},
                           {cellW, cellH}, c);
            }
        }

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

    bool IsRenderItemVisible(const RenderItem& it) const {
        if (!m_ctx->fogOfWar) return true;
        if (it.kind == RenderItem::Kind::Player) return true;
        int tx = 0, ty = 0;
        switch (it.kind) {
            case RenderItem::Kind::Tile:
                tx = it.tx; ty = it.ty; break;
            case RenderItem::Kind::Monster:
                tx = static_cast<int>(std::lround(it.monster->pos.x));
                ty = static_cast<int>(std::lround(it.monster->pos.y));
                break;
            case RenderItem::Kind::GroundItem:
                tx = static_cast<int>(std::lround(it.groundItem->pos.x));
                ty = static_cast<int>(std::lround(it.groundItem->pos.y));
                break;
            case RenderItem::Kind::Player: return true;
        }
        if (tx < 0 || ty < 0 || tx >= m_grid.w || ty >= m_grid.h) return false;
        return m_visible[static_cast<size_t>(ty) * m_grid.w + tx];
    }

    std::vector<bool> m_visible;          // recomputed every frame in OnUpdate
    bool  m_inventoryOpen   = false;
    float m_playerHurtFlash = 0.0f;   // ticked down by combat code; gated on player tint

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
                      "TOWN  -  WASD walk onto portal  -  I inventory  -  V fog %s  -  visits %d  bag %d",
                      m_ctx->fogOfWar ? "on" : "off",
                      m_ctx->dungeonVisits, (int)m_ctx->inventory.size());
        return buf;
    }
};

class DungeonScene : public WorldScene {
public:
    DungeonScene(GameContext* ctx) : WorldScene(ctx) {}

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
                if (!TileBlocksMovement(m_grid.At(
                        (int)std::floor(m.pos.x + stepX), (int)std::floor(m.pos.y))))
                    m.pos.x += stepX;
                if (!TileBlocksMovement(m_grid.At(
                        (int)std::floor(m.pos.x), (int)std::floor(m.pos.y + stepY))))
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

    void DrawWorldOverlay() override {
        if (m_slashTimer <= 0.0f) return;
        // Slash: a soft translucent quad one tile in front of the player,
        // fades out as the timer ticks down. World-space, not HUD-space, so
        // it scrolls with the camera.
        Renderer2D& r = *m_ctx->renderer;
        const Vec2 fd = FacingDir(m_facing);
        const Vec2 worldHit = { m_player.x + fd.x, m_player.y + fd.y };
        const Vec2 screen = TileToScreen(worldHit.x, worldHit.y, m_camera);
        const float alpha = m_slashTimer / kSlashDuration;
        const float w = 44.0f, h = 24.0f;
        r.DrawQuad({screen.x - w * 0.5f, screen.y - h * 0.5f}, {w, h},
                   Color(1.0f, 0.95f, 0.7f, 0.55f * alpha));
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
        // Row 1 (y=32..127): tall things, 64x96 each
        m_sprites.Add("wall",       0,   32, 64, 96);
        m_sprites.Add("portal",     64,  32, 64, 96);
        // Row 2 (y=128..175): player, 32x48 each
        m_sprites.Add("playerS",    0,   128, 32, 48);
        m_sprites.Add("playerN",    32,  128, 32, 48);
        m_sprites.Add("playerE",    64,  128, 32, 48);
        m_sprites.Add("playerW",    96,  128, 32, 48);
        // Row 3 (y=176..223): monsters, 32x48 each
        m_sprites.Add("skeleton",   0,   176, 32, 48);
        m_sprites.Add("zombie",     32,  176, 32, 48);

        m_ctx.renderer = &m_renderer;
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
