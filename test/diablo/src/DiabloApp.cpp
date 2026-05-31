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
// ---------------------------------------------------------------------------
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

    void OnEnter() override { EnterWorld(); }

    void OnUpdate(float dtRaw) override {
        const float dt = Min(dtRaw, 1.0f / 30.0f);
        if (Input::IsKeyPressed(Key::Escape)) {
            Manager().Change(m_ctx->makeTitle());
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

        // Interact on E. Use NEAREST-tile (round) rather than floor so the
        // player doesn't have to stop perfectly on the cell -- iso movement
        // ends at sub-cell offsets and "I'm clearly standing on the portal"
        // should still register.
        if (Input::IsKeyPressed(Key::E)) {
            const int tx = (int)std::lround(m_player.x);
            const int ty = (int)std::lround(m_player.y);
            const Tile t = m_grid.At(tx, ty);
            if (t == Tile::Portal || t == Tile::Stairs) OnInteract();
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
        // doesn't cover the screen.
        r.DrawQuad({0.0f, 0.0f}, {m_ctx->screenW, m_ctx->screenH},
                   Color(0.06f, 0.05f, 0.08f, 1.0f));

        // Draw tiles in painter order (back-to-front = ascending (tx + ty)).
        // The player slots in by its own (tx+ty) so walls in front occlude it.
        struct Item {
            float sortKey;
            int kind;   // 0 tile, 1 player
            int tx, ty;
            Tile tile;
        };
        std::vector<Item> items;
        items.reserve(m_grid.w * m_grid.h + 1);
        for (int y = 0; y < m_grid.h; ++y) {
            for (int x = 0; x < m_grid.w; ++x) {
                const Tile t = m_grid.At(x, y);
                if (t == Tile::Empty) continue;
                items.push_back({(float)(x + y) - 0.001f, 0, x, y, t});
            }
        }
        items.push_back({m_player.x + m_player.y, 1, 0, 0, Tile::Empty});
        std::sort(items.begin(), items.end(),
                  [](const Item& a, const Item& b) { return a.sortKey < b.sortKey; });

        for (const Item& it : items) {
            if (it.kind == 0) DrawTile(it.tx, it.ty, it.tile);
            else              DrawPlayer();
        }

        // HUD.
        r.DrawText(*m_ctx->font, HudLine(), {16.0f, 12.0f}, Color::White(), 20.0f);

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
        auto drawTall = [&](const TextureRegion& region) {
            // Tall sprites (64x96) anchor with the BOTTOM of the diamond at
            // (center.x, center.y + halfH). The top of the sprite then sits
            // at y = center.y + halfH - 96.
            const Vec2 pos{center.x - kHalfTileW,
                           center.y + kHalfTileH - kWallSpriteH};
            r.DrawQuad(pos, {kTileW, kWallSpriteH}, region);
        };

        switch (t) {
            case Tile::Floor:  drawFloor(m_ctx->sprites->Get("floorStone")); break;
            case Tile::Grass:  drawFloor(m_ctx->sprites->Get("floorGrass")); break;
            case Tile::Dirt:   drawFloor(m_ctx->sprites->Get("floorDirt"));  break;
            case Tile::Stairs: drawFloor(m_ctx->sprites->Get("stairs"));     break;
            case Tile::Wall:   drawTall (m_ctx->sprites->Get("wall"));       break;
            case Tile::Portal:
                // Portal floats above a floor tile -- draw stone first, then the
                // glow over it so the portal looks "set into" the ground.
                drawFloor(m_ctx->sprites->Get("floorStone"));
                drawTall (m_ctx->sprites->Get("portal"));
                break;
            case Tile::Empty: break;
        }
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
        r.DrawQuad(pos, {kPlayerSpriteW, kPlayerSpriteH}, reg);
    }

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
    }
    void OnInteract() override {
        ++m_ctx->dungeonVisits;
        m_ctx->rngSeed = m_ctx->rngSeed * 1103515245u + 12345u;
        Manager().Change(m_ctx->makeDungeon());
    }
    std::string HudLine() const override {
        return "TOWN  -  WASD move, E enter portal, Esc title  -  visits " +
               std::to_string(m_ctx->dungeonVisits);
    }
};

class DungeonScene : public WorldScene {
public:
    DungeonScene(GameContext* ctx) : WorldScene(ctx) {}

protected:
    void EnterWorld() override {
        DungeonGenResult g = GenerateDungeon(m_ctx->rngSeed);
        m_grid   = std::move(g.grid);
        m_player = g.playerSpawn;
        m_facing = 0;
    }
    void OnInteract() override { Manager().Change(m_ctx->makeTown()); }
    std::string HudLine() const override {
        return "DUNGEON  -  WASD move, E stairs back to town, Esc title  -  seed " +
               std::to_string(m_ctx->rngSeed);
    }
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
        m_ctx->DrawTextCentered("WASD move  -  E interact  -  Esc title",
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
