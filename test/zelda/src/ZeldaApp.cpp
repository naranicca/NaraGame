// Zelda-like room explorer. Loads .ntmap rooms (authored with the retired
// standalone tileset-editor; the format lives on in assets/TileLevel),
// transitions between them via door markers carrying free-form metadata
// ("to=room2,side=west") in the editor's per-marker data field. Purpose: a
// thin test game that exercises the editor's multi-map, marker-metadata,
// and autotile pipelines end-to-end. Treat the code shape as informative
// only — collision, combat, etc. are deliberately minimal.
//
// Controls:
//   Title:      Enter start, Esc quit
//   In-room:    WASD / arrows move, Space sword, Esc -> title
//   Game over:  Enter restart, Esc title

#include <NaraGame/assets/AssetWatcher.h>
#include <NaraGame/assets/MSDFFontLoader.h>
#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/assets/TilemapLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/renderer/Camera2D.h>
#include <NaraGame/renderer/MSDFFont.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/SpriteSheet.h>
#include <NaraGame/renderer/Tilemap.h>
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
// Screen + world.
// ---------------------------------------------------------------------------
constexpr float kScreenW   = 1280.0f;
constexpr float kScreenH   = 720.0f;
constexpr float kTilePx    = 32.0f;     // world units per tile (matches tiles.png cell)
constexpr float kZoom      = 1.875f;    // 32 -> 60px on screen; 12 rows * 60 = 720
constexpr int   kRoomTilesW = 20;
constexpr int   kRoomTilesH = 12;

// Player.
constexpr float kPlayerR     = 11.0f;
constexpr float kPlayerSpeed = 115.0f;
constexpr float kPlayerHurtT = 1.20f;    // post-hit invulnerability -- long enough
                                         // for the player to walk well clear of
                                         // a slime before the next hit lands.
constexpr float kPlayerSpawnInset = 14.0f;  // distance from door tile center on entry

// Sword swing -- thin rectangle in front of the player.
constexpr float kSwingDur   = 0.22f;
constexpr float kSwingReach = 30.0f;
constexpr float kSwingHalfW = 14.0f;

// Slime. Tuned to be a low threat -- the goal is exercising the editor's
// .ntmap + marker pipeline, not perfecting combat. The 40px aggro radius
// (~1.25 tiles) plus 20px/s chase means a slime that catches the player
// at all gets exactly one hit in before the 1.2s i-frame lets them walk
// away clean.
constexpr float kSlimeR        = 10.0f;
constexpr float kSlimeSpeed    = 20.0f;
constexpr float kSlimeWanderCd = 1.6f;
constexpr float kSlimeHurtT    = 0.12f;
constexpr float kSlimeAggroR   = 40.0f;
constexpr float kSpawnInvincT  = 1.0f;    // post-entry grace window

// HUD heart strip.
constexpr float kHudPad   = 24.0f;
constexpr float kHeartPx  = 40.0f;
constexpr int   kStartHp  = 4;

// Link sprite sheet -- link.png is a hand-laid sheet with non-uniform frame
// rects, so we hard-code pixel boxes. Layout per the source artist:
//   row 1: S-idle  x3   row 5: S-walk x10
//   row 2: W-idle  x3   row 6: W-walk x10
//   row 3: N-idle  x1   row 7: N-walk x10
//   row 4: E-idle  x3   row 8: E-walk x10
// Idle uses only the first frame per direction; walk cycles all 10.
struct LinkRect { int x, y, w, h; };

constexpr LinkRect kLinkIdle[4] = {  // indexed by Facing (S/W/N/E = 0/1/2/3)
    {  5,   9, 58, 72},
    { 11,  92, 55, 70},
    {  9, 175, 57, 67},
    {  9, 253, 54, 70},
};
constexpr int kLinkWalkFrames = 10;
constexpr LinkRect kLinkWalk[4][kLinkWalkFrames] = {
    {  // S-walk
        {  9, 331, 57, 72}, { 83, 327, 57, 70}, {157, 327, 57, 76},
        {232, 327, 57, 76}, {306, 327, 57, 76}, {380, 331, 57, 72},
        {454, 327, 57, 70}, {529, 327, 57, 76}, {603, 327, 57, 76},
        {677, 327, 57, 76},
    },
    {  // W-walk
        {  2, 414, 73, 68}, { 80, 414, 60, 67}, {151, 411, 60, 63},
        {228, 411, 61, 70}, {303, 411, 63, 70}, {374, 414, 73, 68},
        {451, 414, 60, 68}, {525, 411, 58, 67}, {603, 411, 60, 67},
        {674, 411, 64, 70},
    },
    {  // N-walk
        {  9, 482, 57, 76}, { 83, 488, 57, 70}, {157, 482, 57, 76},
        {232, 482, 57, 76}, {306, 482, 57, 76}, {380, 482, 57, 76},
        {454, 482, 57, 76}, {529, 482, 57, 76}, {603, 485, 57, 76},
        {677, 482, 57, 76},
    },
    {  // E-walk
        {  2, 572, 64, 70}, { 83, 572, 60, 70}, {157, 572, 61, 70},
        {228, 575, 61, 69}, {296, 575, 73, 69}, {377, 572, 63, 70},
        {451, 572, 60, 70}, {532, 572, 60, 63}, {606, 575, 61, 67},
        {668, 575, 73, 69},
    },
};

// Quarter-view anchor: sprite is one tile tall and sits with its bottom
// flush against the collision circle's bottom, so the feet line up with
// what the player can actually push into. Because the sprite is taller
// than the collision diameter (32 vs 22), the head still rises past the
// tile top -- enough to read as perspective without floating off the
// ground.
constexpr float kPlayerSpriteH  = 32.0f;
constexpr float kPlayerFeetDrop = 0.0f;
constexpr float kWalkAnimFps   = 12.0f;

enum class Facing : int { S = 0, W = 1, N = 2, E = 3 };

// Tile-id constants matching generate-assets.ps1.
constexpr int kTileGrass     = 0;
constexpr int kTileDoorFloor = 95;
constexpr int kTileLockedDoor = 96;

// Collision codes (engine convention).
constexpr uint8_t kCollFloor = 0;
constexpr uint8_t kCollWall  = 1;
constexpr uint8_t kCollWater = 2;

enum class CardinalDir { N, E, S, W };

CardinalDir OppositeDir(CardinalDir d) {
    switch (d) {
        case CardinalDir::N: return CardinalDir::S;
        case CardinalDir::E: return CardinalDir::W;
        case CardinalDir::S: return CardinalDir::N;
        case CardinalDir::W: return CardinalDir::E;
    }
    return CardinalDir::S;
}

CardinalDir ParseSide(const std::string& s) {
    if (s == "north" || s == "n") return CardinalDir::N;
    if (s == "east"  || s == "e") return CardinalDir::E;
    if (s == "south" || s == "s") return CardinalDir::S;
    if (s == "west"  || s == "w") return CardinalDir::W;
    return CardinalDir::S;
}

// Pull a `key=value` field out of a marker's free-form data string.
// Returns "" if the key isn't present. Tolerates the trailing-value
// case (no comma after the last pair) and ignores whitespace.
std::string MarkerField(const std::string& data, const std::string& key) {
    size_t i = 0;
    while (i < data.size()) {
        while (i < data.size() && (data[i] == ' ' || data[i] == '\t')) ++i;
        const size_t eq = data.find('=', i);
        if (eq == std::string::npos) break;
        const std::string k = data.substr(i, eq - i);
        const size_t comma = data.find(',', eq + 1);
        std::string v = (comma == std::string::npos)
            ? data.substr(eq + 1)
            : data.substr(eq + 1, comma - eq - 1);
        // Trim trailing spaces from the value.
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' ||
                              v.back() == '\r' || v.back() == '\n')) v.pop_back();
        if (k == key) return v;
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Shared state between scenes.
// ---------------------------------------------------------------------------
struct GameContext {
    Renderer2D*  renderer = nullptr;
    MSDFFont*    font     = nullptr;
    RHIDevice*   device   = nullptr;
    std::function<std::unique_ptr<Scene>()> makeTitle;
    std::function<std::unique_ptr<Scene>(int, CardinalDir)> makeRoom;
    std::function<std::unique_ptr<Scene>(bool)> makeGameOver;

    // Run state.
    int  hp        = kStartHp;
    bool hasKey    = false;
    int  roomId    = 1;
    CardinalDir entrySide = CardinalDir::S;
    bool freshRun  = true;    // true until the player enters their first room
                              // -- @ marker is honored only on a fresh run.

    // Sprite sheet for slime/key/heart/treasure. The textures live on the
    // application (so they survive scene swaps) and are referenced by
    // RoomScene on draw. Player uses linkTex instead (non-uniform frames
    // so it bypasses SpriteSheet -- see kLinkIdle/kLinkWalk above).
    std::unique_ptr<RHITexture> charsTex;
    SpriteSheet                 charSheet;
    std::unique_ptr<RHITexture> linkTex;
};

inline TextureRegion CharSprite(GameContext* ctx, int idx) {
    if (!ctx || !ctx->charsTex) return {};
    return ctx->charSheet.At(idx);
}

void DrawCentered(GameContext* ctx, const std::string& s, float y,
                  Color c, float px) {
    if (!ctx || !ctx->font || !ctx->renderer) return;
    const Vec2 sz = ctx->font->Measure(s, px);
    ctx->renderer->DrawText(*ctx->font, s,
                            {(kScreenW - sz.x) * 0.5f, y}, c, px);
}

// ---------------------------------------------------------------------------
// TitleScene.
// ---------------------------------------------------------------------------
class TitleScene : public Scene {
public:
    explicit TitleScene(GameContext* ctx) : m_ctx(ctx) {}

    void OnEnter() override { m_pulse = 0.0f; }

    void OnUpdate(float dt) override {
        m_pulse += dt;
        if (Input::IsKeyPressed(Key::Enter) || Input::IsKeyPressed(Key::Space)) {
            m_ctx->hp        = kStartHp;
            m_ctx->hasKey    = false;
            m_ctx->roomId    = 1;
            m_ctx->entrySide = CardinalDir::S;
            m_ctx->freshRun  = true;
            Manager().ChangeWith(m_ctx->makeRoom(1, CardinalDir::S), 0.35f);
        }
        if (Input::IsKeyPressed(Key::Escape)) Application::Get().Close();
    }

    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;
        r.Begin(Renderer2D::ScreenCamera(kScreenW, kScreenH));

        // Layered band background -- evokes the dungeon entrance.
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH * 0.55f},
                   Color(0.06f, 0.10f, 0.16f, 1.0f));
        r.DrawQuad({0.0f, kScreenH * 0.55f}, {kScreenW, kScreenH * 0.30f},
                   Color(0.12f, 0.32f, 0.20f, 1.0f));
        r.DrawQuad({0.0f, kScreenH * 0.85f}, {kScreenW, kScreenH * 0.15f},
                   Color(0.08f, 0.20f, 0.14f, 1.0f));

        DrawCentered(m_ctx, "NARA QUEST", 200.0f,
                     Color(1.0f, 0.95f, 0.55f, 1.0f), 88.0f);
        DrawCentered(m_ctx, "A tiny room explorer", 310.0f,
                     Color(0.85f, 0.85f, 0.90f, 1.0f), 22.0f);

        const float blink = 0.5f + 0.5f * std::sin(m_pulse * 3.0f);
        DrawCentered(m_ctx, "PRESS ENTER TO START", 460.0f,
                     Color(1.0f, 1.0f, 1.0f, blink), 30.0f);
        DrawCentered(m_ctx,
                     "WASD/Arrows: move    Space: sword    Esc: quit",
                     560.0f, Color(0.78f, 0.82f, 0.90f, 1.0f), 18.0f);
        r.End();
    }

private:
    GameContext* m_ctx = nullptr;
    float m_pulse = 0.0f;
};

// ---------------------------------------------------------------------------
// GameOverScene -- wins and losses share the same scene with a `won` flag.
// ---------------------------------------------------------------------------
class GameOverScene : public Scene {
public:
    GameOverScene(GameContext* ctx, bool won) : m_ctx(ctx), m_won(won) {}

    void OnUpdate(float /*dt*/) override {
        if (Input::IsKeyPressed(Key::Enter) || Input::IsKeyPressed(Key::Space)) {
            // Both win and lose -> back to the title; the title resets HP /
            // key state on entry to the next room.
            Manager().ChangeWith(m_ctx->makeTitle(), 0.4f);
        }
        if (Input::IsKeyPressed(Key::Escape)) {
            Manager().ChangeWith(m_ctx->makeTitle(), 0.4f);
        }
    }

    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;
        r.Begin(Renderer2D::ScreenCamera(kScreenW, kScreenH));
        const Color bg = m_won
            ? Color(0.18f, 0.16f, 0.06f, 1.0f)
            : Color(0.12f, 0.04f, 0.06f, 1.0f);
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH}, bg);
        DrawCentered(m_ctx, m_won ? "YOU WIN" : "GAME OVER", 260.0f,
                     m_won ? Color(1.0f, 0.95f, 0.55f, 1.0f)
                           : Color(1.0f, 0.55f, 0.55f, 1.0f),
                     96.0f);
        DrawCentered(m_ctx, "Press Enter to return to title",
                     420.0f, Color(0.92f, 0.92f, 0.92f, 1.0f), 26.0f);
        r.End();
    }

private:
    GameContext* m_ctx = nullptr;
    bool m_won = false;
};

// ---------------------------------------------------------------------------
// RoomScene -- the main gameplay loop. Loads one .ntmap, runs movement +
// combat + room transitions, and exits to GameOver on death/win.
// ---------------------------------------------------------------------------
class RoomScene : public Scene {
public:
    RoomScene(GameContext* ctx, int roomId, CardinalDir entrySide)
        : m_ctx(ctx), m_roomId(roomId), m_entrySide(entrySide) {
        m_rng.seed(std::random_device{}());
    }

    void OnEnter() override {
        m_ctx->roomId    = m_roomId;
        m_ctx->entrySide = m_entrySide;
        NARAGAME_INFO("zelda: enter room%d from %s",
                      m_roomId,
                      m_entrySide == CardinalDir::N ? "north" :
                      m_entrySide == CardinalDir::E ? "east"  :
                      m_entrySide == CardinalDir::S ? "south" : "west");
        LoadRoomAsset();

        m_camera = Camera2D(kScreenW, kScreenH);
        m_camera.SetZoom(kZoom);
        // Center on the room midpoint. The map is wider than the visible
        // world area horizontally (640 vs ~683), so the player tracks
        // through the room without scrolling -- this room scope is small
        // by design and not meant to scroll like brawl.
        const float roomW = float(kRoomTilesW) * kTilePx;
        const float roomH = float(kRoomTilesH) * kTilePx;
        m_camera.CenterOn({roomW * 0.5f, roomH * 0.5f});

        SpawnPlayerForEntry();
        BuildEntities();
        // Brief invulnerability so the player can orient before the first
        // slime contact -- avoids dying on entry if a slime happens to be
        // wandering near the spawn cell.
        m_playerHurt = kSpawnInvincT;

        // Hot reload: watch the assets dir for .ntmap and tiles.png changes;
        // the editor's saves land here within a frame or two.
        if (!m_watcherInit) {
            m_watcherInit = true;
            const std::string assets = NARAGAME_ASSETS_DIR;
            m_watcher.Watch(assets, [this](const std::string& rel) {
                if (rel == "tiles.png" || rel.find(".ntmap") != std::string::npos) {
                    m_reloadPending = true;
                }
            });
        }
    }

    void OnUpdate(float dt) override {
        // Drain hot-reload events first so a paint-and-save in the editor
        // shows up before this frame's gameplay reads collision.
        m_watcher.Poll();
        if (m_reloadPending) {
            m_reloadPending = false;
            const Vec2 savedPos = m_playerPos;
            const float savedFacing = m_playerFacingX;
            LoadRoomAsset();
            m_playerPos = savedPos;
            m_playerFacingX = savedFacing;
            BuildEntities();
        }

        if (Input::IsKeyPressed(Key::Escape)) {
            Manager().ChangeWith(m_ctx->makeTitle(), 0.3f);
            return;
        }

        // Debug teleport: number keys 1-4 jump straight to that room. Uses
        // the same Manager().ChangeWith path the door triggers do, so the
        // transition wiring stays exercised. Entry side is fixed to S so
        // the player drops at whichever door faces south (or falls back to
        // the @ marker on a fresh run).
        const Key kRoomKeys[4] = {Key::Num1, Key::Num2, Key::Num3, Key::Num4};
        for (int i = 0; i < 4; ++i) {
            if (Input::IsKeyPressed(kRoomKeys[i])) {
                Manager().ChangeWith(
                    m_ctx->makeRoom(i + 1, CardinalDir::S), 0.25f);
                return;
            }
        }
        // Debug cheat: K key grants the dungeon key. Used to verify the
        // room2 east locked door unlock without having to physically
        // navigate to room3's key first.
        if (Input::IsKeyPressed(Key::K)) {
            m_ctx->hasKey = true;
            NARAGAME_INFO("zelda: debug -- granted dungeon key");
        }

        UpdatePlayer(dt);
        UpdateSlimes(dt);
        UpdateSwing(dt);
        CheckPickups();
        CheckHazards(dt);
        CheckDoors(dt);
        CheckWin();
    }

    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;

        // World pass: tilemap + sprites at camera transform.
        r.Begin(m_camera.ViewProjection());
        if (m_asset.valid) {
            DrawAllLayers(m_asset, r, m_camera.VisibleBounds(),
                          Color::White());
        }
        DrawObjectMarkersInWorld(r);
        DrawSlimes(r);
        DrawPlayer(r);
        DrawSwing(r);
        r.End();

        // HUD pass: hearts + status text.
        r.Begin(Renderer2D::ScreenCamera(kScreenW, kScreenH));
        DrawHud(r);
        r.End();
    }

private:
    // ---- asset loading -----------------------------------------------------
    void LoadRoomAsset() {
        const std::string assets = NARAGAME_ASSETS_DIR;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "/room%d.ntmap", m_roomId);
        const std::string path = assets + buf;
        m_asset = LoadNtmap(*m_ctx->device, path, TextureFilter::Point);
        if (!m_asset.valid) {
            NARAGAME_ERROR("zelda: failed to load %s", path.c_str());
        }
        // Cache collision codes for quick movement queries. Empty if the
        // file didn't carry a collision section.
        m_coll = m_asset.collision;
        m_mapW = m_asset.valid ? m_asset.map.Width()  : 0;
        m_mapH = m_asset.valid ? m_asset.map.Height() : 0;
    }

    uint8_t CollAt(int tx, int ty) const {
        // OOB reads as wall so the player can't slip off the map edge.
        if (tx < 0 || ty < 0 || tx >= m_mapW || ty >= m_mapH) return kCollWall;
        if (m_coll.empty()) return kCollFloor;
        return m_coll[size_t(ty) * size_t(m_mapW) + size_t(tx)];
    }

    bool CellBlocksPlayer(int tx, int ty) const {
        const uint8_t c = CollAt(tx, ty);
        if (c == kCollWall || c == kCollWater) return true;
        // Locked door tile: passable only with key. Collision codes don't
        // model "lockable", so we check the tile id directly.
        if (m_asset.valid &&
            tx >= 0 && ty >= 0 && tx < m_mapW && ty < m_mapH) {
            const int id = m_asset.map.GetTile(tx, ty);
            if (id == kTileLockedDoor && !m_ctx->hasKey) return true;
        }
        return false;
    }

    // Circle-vs-tile-grid test borrowed from brawl. True if the circle at
    // (wx, wy) overlaps any blocking cell.
    bool CircleBlocked(float wx, float wy, float r) const {
        const int minTx = int(std::floor((wx - r) / kTilePx));
        const int maxTx = int(std::floor((wx + r) / kTilePx));
        const int minTy = int(std::floor((wy - r) / kTilePx));
        const int maxTy = int(std::floor((wy + r) / kTilePx));
        for (int ty = minTy; ty <= maxTy; ++ty) {
            for (int tx = minTx; tx <= maxTx; ++tx) {
                if (!CellBlocksPlayer(tx, ty)) continue;
                const float cellL = float(tx) * kTilePx;
                const float cellT = float(ty) * kTilePx;
                const float cellR = cellL + kTilePx;
                const float cellB = cellT + kTilePx;
                const float cx = std::max(cellL, std::min(wx, cellR));
                const float cy = std::max(cellT, std::min(wy, cellB));
                const float dx = wx - cx, dy = wy - cy;
                if (dx * dx + dy * dy < r * r) return true;
            }
        }
        return false;
    }

    // Move axis-by-axis so the player slides along walls when their
    // diagonal movement clips a corner.
    void MoveWithCollision(Vec2& pos, Vec2 delta, float radius) const {
        float nx = pos.x + delta.x;
        if (CircleBlocked(nx, pos.y, radius)) nx = pos.x;
        float ny = pos.y + delta.y;
        if (CircleBlocked(nx, ny, radius)) ny = pos.y;
        pos.x = nx; pos.y = ny;
    }

    // ---- spawn / entity build ----------------------------------------------
    void SpawnPlayerForEntry() {
        // On a fresh run, prefer the @ marker if the room defines one (so
        // room1 can place the player at a specific spawn). Otherwise drop
        // them just inside the door that matches m_entrySide, which lets
        // re-entry from room transitions land in the right place.
        bool placed = false;
        if (m_ctx->freshRun && m_asset.valid) {
            for (const auto& m : m_asset.objects) {
                if (m.symbol == '@') {
                    m_playerPos = TileCenter(m.x, m.y);
                    placed = true;
                    break;
                }
            }
            m_ctx->freshRun = false;
        }
        if (!placed && m_asset.valid) {
            // Find any door marker on the entry side; spawn just inside it
            // facing into the room.
            ObjectMarker chosen{};
            bool found = false;
            for (const auto& m : m_asset.objects) {
                if (m.symbol != 'D' && m.symbol != 'L') continue;
                const CardinalDir side = SideFromDoorCell(m.x, m.y);
                if (side == m_entrySide) {
                    chosen = m;
                    found = true;
                    break;
                }
            }
            if (found) {
                // Spawn ONE TILE inside the door cell, not on it -- otherwise
                // CheckDoors triggers on the entry frame and ping-pongs the
                // player straight back to the room they just left.
                int sx = chosen.x;
                int sy = chosen.y;
                switch (m_entrySide) {
                    case CardinalDir::N: sy = chosen.y + 1; break;
                    case CardinalDir::S: sy = chosen.y - 1; break;
                    case CardinalDir::E: sx = chosen.x - 1; break;
                    case CardinalDir::W: sx = chosen.x + 1; break;
                }
                m_playerPos = TileCenter(sx, sy);
                placed = true;
            }
        }
        if (!placed) {
            // Fallback: room center.
            m_playerPos = {float(m_mapW) * kTilePx * 0.5f,
                           float(m_mapH) * kTilePx * 0.5f};
        }
        switch (m_entrySide) {
            case CardinalDir::W: m_playerFacingX = 1.0f; break;
            case CardinalDir::E: m_playerFacingX = -1.0f; break;
            default:             m_playerFacingX = 1.0f; break;
        }
    }

    // Which side of the room a door cell sits on, used for entry-side
    // matching. Cells on a corner default to the dominant axis.
    CardinalDir SideFromDoorCell(int x, int y) const {
        const int distN = y, distS = (m_mapH - 1 - y);
        const int distW = x, distE = (m_mapW - 1 - x);
        const int mn = std::min(std::min(distN, distS),
                                std::min(distW, distE));
        if (mn == distN) return CardinalDir::N;
        if (mn == distS) return CardinalDir::S;
        if (mn == distW) return CardinalDir::W;
        return CardinalDir::E;
    }

    Vec2 TileCenter(int x, int y) const {
        return {float(x) * kTilePx + kTilePx * 0.5f,
                float(y) * kTilePx + kTilePx * 0.5f};
    }

    void BuildEntities() {
        m_slimes.clear();
        m_keys.clear();
        m_treasures.clear();
        if (!m_asset.valid) return;
        for (const auto& m : m_asset.objects) {
            switch (m.symbol) {
                case 'E': {
                    Slime s;
                    s.pos = TileCenter(m.x, m.y);
                    s.hp  = 1;
                    s.wanderDir = RandomUnit();
                    s.wanderCd  = kSlimeWanderCd;
                    m_slimes.push_back(s);
                    break;
                }
                case 'K': m_keys.push_back(TileCenter(m.x, m.y)); break;
                case 'T': m_treasures.push_back(TileCenter(m.x, m.y)); break;
                default: break;
            }
        }
    }

    Vec2 RandomUnit() {
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        float x = u(m_rng), y = u(m_rng);
        const float len = std::sqrt(x * x + y * y);
        if (len < 0.001f) return {1.0f, 0.0f};
        return {x / len, y / len};
    }

    // ---- player update -----------------------------------------------------
    void UpdatePlayer(float dt) {
        if (m_playerHurt > 0.0f) m_playerHurt -= dt;

        Vec2 v{0.0f, 0.0f};
        if (Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::Up))    v.y -= 1.0f;
        if (Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::Down))  v.y += 1.0f;
        if (Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::Left))  v.x -= 1.0f;
        if (Input::IsKeyDown(Key::D) || Input::IsKeyDown(Key::Right)) v.x += 1.0f;
        const float len = std::sqrt(v.x * v.x + v.y * v.y);
        if (len > 0.001f) { v.x /= len; v.y /= len; }
        if (std::abs(v.x) > 0.01f) m_playerFacingX = v.x < 0.0f ? -1.0f : 1.0f;

        // 4-dir facing for the Link sprite. On diagonal input, vertical
        // takes precedence -- normalized v.y is +/-0.707 on a diagonal,
        // and the > 0.5 thresholds catch that ahead of the x checks below.
        m_moving = (len > 0.001f);
        if (v.y < -0.5f)      m_facing = Facing::N;
        else if (v.y >  0.5f) m_facing = Facing::S;
        else if (v.x < -0.5f) m_facing = Facing::W;
        else if (v.x >  0.5f) m_facing = Facing::E;
        if (m_moving) m_walkAnimT += dt;
        else          m_walkAnimT  = 0.0f;

        const Vec2 delta{v.x * kPlayerSpeed * dt, v.y * kPlayerSpeed * dt};
        MoveWithCollision(m_playerPos, delta, kPlayerR);

        // Trigger sword swing on space press; can't restart mid-swing.
        if (Input::IsKeyPressed(Key::Space) && m_swingT <= 0.0f) {
            m_swingT = kSwingDur;
            m_swingDirX = m_playerFacingX;
            m_swingHit.assign(m_slimes.size(), false);
        }
    }

    void UpdateSwing(float dt) {
        if (m_swingT <= 0.0f) return;
        m_swingT -= dt;
        // While the swing is live, test the hit box vs every slime that
        // hasn't already been struck this swing.
        const float swingCX = m_playerPos.x +
                              m_swingDirX * (kPlayerR + kSwingReach * 0.5f);
        const float swingCY = m_playerPos.y;
        const float halfW   = kSwingReach * 0.5f + kSwingHalfW;
        const float halfH   = kSwingHalfW;
        if (m_swingHit.size() != m_slimes.size())
            m_swingHit.assign(m_slimes.size(), false);
        for (size_t i = 0; i < m_slimes.size(); ++i) {
            if (m_swingHit[i]) continue;
            Slime& s = m_slimes[i];
            if (s.hp <= 0) continue;
            // Box-circle: clamp slime to swing box.
            const float cx = std::max(swingCX - halfW,
                                std::min(s.pos.x, swingCX + halfW));
            const float cy = std::max(swingCY - halfH,
                                std::min(s.pos.y, swingCY + halfH));
            const float dx = s.pos.x - cx, dy = s.pos.y - cy;
            if (dx * dx + dy * dy < kSlimeR * kSlimeR) {
                s.hp     -= 1;
                s.hurtT   = kSlimeHurtT;
                m_swingHit[i] = true;
            }
        }
    }

    // ---- slimes -----------------------------------------------------------
    void UpdateSlimes(float dt) {
        for (auto& s : m_slimes) {
            if (s.hp <= 0) continue;
            if (s.hurtT > 0.0f) s.hurtT -= dt;
            s.wanderCd -= dt;
            const Vec2 toPlayer{m_playerPos.x - s.pos.x,
                                m_playerPos.y - s.pos.y};
            const float distSq = toPlayer.x * toPlayer.x +
                                 toPlayer.y * toPlayer.y;
            Vec2 dir;
            if (distSq < kSlimeAggroR * kSlimeAggroR) {
                const float d = std::sqrt(distSq);
                dir = (d < 0.001f) ? Vec2{1.0f, 0.0f}
                                   : Vec2{toPlayer.x / d, toPlayer.y / d};
            } else {
                if (s.wanderCd <= 0.0f) {
                    s.wanderDir = RandomUnit();
                    s.wanderCd  = kSlimeWanderCd;
                }
                dir = s.wanderDir;
            }
            const Vec2 delta{dir.x * kSlimeSpeed * dt,
                             dir.y * kSlimeSpeed * dt};
            MoveWithCollision(s.pos, delta, kSlimeR);
        }
        // Sweep dead slimes; tighter to update than tombstoning them.
        m_slimes.erase(
            std::remove_if(m_slimes.begin(), m_slimes.end(),
                           [](const Slime& s) { return s.hp <= 0; }),
            m_slimes.end());
        m_swingHit.assign(m_slimes.size(), false);
    }

    // ---- pickups + hazards --------------------------------------------------
    void CheckPickups() {
        const float r2 = (kPlayerR + 14.0f) * (kPlayerR + 14.0f);
        for (auto it = m_keys.begin(); it != m_keys.end(); ) {
            const float dx = it->x - m_playerPos.x;
            const float dy = it->y - m_playerPos.y;
            if (dx * dx + dy * dy < r2) {
                m_ctx->hasKey = true;
                it = m_keys.erase(it);
            } else {
                ++it;
            }
        }
    }

    void CheckHazards(float /*dt*/) {
        if (m_playerHurt > 0.0f) return;
        const float r2 = (kPlayerR + kSlimeR) * (kPlayerR + kSlimeR);
        for (const auto& s : m_slimes) {
            if (s.hp <= 0) continue;
            const float dx = s.pos.x - m_playerPos.x;
            const float dy = s.pos.y - m_playerPos.y;
            if (dx * dx + dy * dy < r2) {
                m_ctx->hp     -= 1;
                m_playerHurt   = kPlayerHurtT;
                if (m_ctx->hp <= 0) {
                    Manager().ChangeWithFadeColor(
                        m_ctx->makeGameOver(false),
                        Color(0.3f, 0.0f, 0.0f, 1.0f), 0.5f);
                }
                break;
            }
        }
    }

    // ---- doors + transitions -----------------------------------------------
    void CheckDoors(float /*dt*/) {
        if (!m_asset.valid) return;
        const int ptx = int(std::floor(m_playerPos.x / kTilePx));
        const int pty = int(std::floor(m_playerPos.y / kTilePx));
        for (const auto& m : m_asset.objects) {
            if (m.symbol != 'D' && m.symbol != 'L') continue;
            if (m.x != ptx || m.y != pty) continue;

            // Locked door: blocked by CellBlocksPlayer unless the player has
            // the key. If we got here with hasKey we also "consume the lock"
            // visually -- the next-room load will redraw the unlock state
            // anyway (we just don't write back to the .ntmap).
            if (m.symbol == 'L' && !m_ctx->hasKey) return;

            const std::string to   = MarkerField(m.data, "to");
            const std::string side = MarkerField(m.data, "side");
            if (to.empty()) continue;
            // Parse room number from "roomN".
            int nextRoom = 0;
            for (size_t i = 0; i < to.size(); ++i) {
                if (to[i] >= '0' && to[i] <= '9') {
                    nextRoom = nextRoom * 10 + (to[i] - '0');
                }
            }
            if (nextRoom <= 0) continue;
            const CardinalDir entry = ParseSide(side);
            NARAGAME_INFO("zelda: door %c at (%d,%d) -> room%d enter from %s",
                          m.symbol, m.x, m.y, nextRoom, side.c_str());
            Manager().ChangeWith(m_ctx->makeRoom(nextRoom, entry), 0.35f);
            return;
        }
    }

    void CheckWin() {
        if (m_treasures.empty()) return;
        const float r2 = (kPlayerR + 16.0f) * (kPlayerR + 16.0f);
        for (const auto& t : m_treasures) {
            const float dx = t.x - m_playerPos.x;
            const float dy = t.y - m_playerPos.y;
            if (dx * dx + dy * dy < r2) {
                Manager().ChangeWithFadeColor(
                    m_ctx->makeGameOver(true),
                    Color(1.0f, 0.95f, 0.55f, 1.0f), 0.6f);
                return;
            }
        }
    }

    // ---- drawing -----------------------------------------------------------
    void DrawObjectMarkersInWorld(Renderer2D& r) {
        // Drawn in world coords so they zoom with the camera. Markers we
        // don't have explicit sprites for fall back to a small coloured box.
        const TextureRegion key = CharSprite(m_ctx, 2);
        const TextureRegion chest = CharSprite(m_ctx, 4);
        for (const auto& p : m_keys) {
            if (key.Valid()) {
                r.DrawQuad({p.x - 12.0f, p.y - 12.0f},
                           {24.0f, 24.0f}, key, Color::White());
            } else {
                r.DrawQuad({p.x - 8.0f, p.y - 8.0f},
                           {16.0f, 16.0f},
                           Color(1.0f, 0.92f, 0.30f, 1.0f));
            }
        }
        for (const auto& p : m_treasures) {
            if (chest.Valid()) {
                r.DrawQuad({p.x - 14.0f, p.y - 14.0f},
                           {28.0f, 28.0f}, chest, Color::White());
            } else {
                r.DrawQuad({p.x - 12.0f, p.y - 12.0f},
                           {24.0f, 24.0f},
                           Color(1.0f, 0.80f, 0.25f, 1.0f));
            }
        }
    }

    void DrawSlimes(Renderer2D& r) {
        const TextureRegion sprite = CharSprite(m_ctx, 1);
        for (const auto& s : m_slimes) {
            Color tint = Color::White();
            if (s.hurtT > 0.0f) tint = Color(1.4f, 0.7f, 0.7f, 1.0f);
            if (sprite.Valid()) {
                r.DrawQuad({s.pos.x - kSlimeR - 2.0f,
                            s.pos.y - kSlimeR - 4.0f},
                           {kSlimeR * 2.0f + 4.0f, kSlimeR * 2.0f + 6.0f},
                           sprite, tint);
            } else {
                r.DrawQuad({s.pos.x - kSlimeR, s.pos.y - kSlimeR},
                           {kSlimeR * 2.0f, kSlimeR * 2.0f},
                           Color(0.40f, 0.80f, 0.70f, 1.0f));
            }
        }
    }

    void DrawPlayer(Renderer2D& r) {
        Color tint = Color::White();
        // Flash white on hit; using a slightly off-white when invulnerable.
        if (m_playerHurt > 0.0f) {
            const float k = (std::sin(m_playerHurt * 40.0f) + 1.0f) * 0.5f;
            tint = Color(1.0f, 1.0f - 0.6f * k, 1.0f - 0.6f * k, 1.0f);
        }
        const int dir = static_cast<int>(m_facing);
        const LinkRect lr = m_moving
            ? kLinkWalk[dir][int(m_walkAnimT * kWalkAnimFps) % kLinkWalkFrames]
            : kLinkIdle[dir];
        if (m_ctx->linkTex) {
            const TextureRegion sprite = TextureRegion::FromPixels(
                m_ctx->linkTex.get(),
                float(lr.x), float(lr.y), float(lr.w), float(lr.h));
            // Quarter-view anchor: sprite bottom sits at the collision
            // circle's bottom (feet on ground), so the head extends upward
            // into the tile above -- not centered on the footprint.
            const float aspect = float(lr.w) / float(lr.h);
            const float h = kPlayerSpriteH;
            const float w = h * aspect;
            const float bottomY = m_playerPos.y + kPlayerR + kPlayerFeetDrop;
            r.DrawQuad({m_playerPos.x - w * 0.5f, bottomY - h},
                       {w, h}, sprite, tint);
        } else {
            r.DrawQuad({m_playerPos.x - kPlayerR, m_playerPos.y - kPlayerR},
                       {kPlayerR * 2.0f, kPlayerR * 2.0f}, tint);
        }
    }

    void DrawSwing(Renderer2D& r) {
        if (m_swingT <= 0.0f) return;
        const float a = std::max(0.0f, m_swingT / kSwingDur);
        const float cx = m_playerPos.x + m_swingDirX *
                                          (kPlayerR + kSwingReach * 0.5f);
        const float cy = m_playerPos.y;
        const Color body(1.0f, 0.95f, 0.55f, 0.55f * a + 0.25f);
        r.DrawQuad({cx - kSwingReach * 0.5f - kSwingHalfW,
                    cy - kSwingHalfW},
                   {kSwingReach + kSwingHalfW * 2.0f,
                    kSwingHalfW * 2.0f},
                   body);
        const Color blade(1.0f, 1.0f, 1.0f, 0.95f);
        r.DrawQuad({cx - kSwingReach * 0.5f,
                    cy - 2.0f},
                   {kSwingReach,
                    4.0f},
                   blade);
    }

    void DrawHud(Renderer2D& r) {
        const TextureRegion heart = CharSprite(m_ctx, 3);
        for (int i = 0; i < kStartHp; ++i) {
            const float x = kHudPad + float(i) * (kHeartPx + 6.0f);
            const float y = kHudPad;
            const bool filled = (i < m_ctx->hp);
            const Color tint = filled
                ? Color::White()
                : Color(0.25f, 0.10f, 0.12f, 1.0f);
            if (heart.Valid()) {
                r.DrawQuad({x, y}, {kHeartPx, kHeartPx}, heart, tint);
            } else {
                r.DrawQuad({x, y}, {kHeartPx, kHeartPx},
                           filled ? Color(0.92f, 0.30f, 0.34f, 1.0f)
                                  : Color(0.30f, 0.10f, 0.12f, 1.0f));
            }
        }
        if (m_ctx->hasKey) {
            const TextureRegion key = CharSprite(m_ctx, 2);
            const float x = kScreenW - kHudPad - kHeartPx;
            const float y = kHudPad;
            if (key.Valid()) {
                r.DrawQuad({x, y}, {kHeartPx, kHeartPx},
                           key, Color::White());
            } else {
                r.DrawQuad({x, y}, {kHeartPx, kHeartPx},
                           Color(1.0f, 0.92f, 0.30f, 1.0f));
            }
        }
        if (m_ctx->font) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "room %d", m_ctx->roomId);
            r.DrawText(*m_ctx->font, buf,
                       {kScreenW * 0.5f - 40.0f, kHudPad - 2.0f},
                       Color(0.85f, 0.87f, 0.92f, 0.85f), 22.0f);
        }
    }

    // ---- data --------------------------------------------------------------
    struct Slime {
        Vec2 pos{0.0f, 0.0f};
        int  hp = 1;
        float hurtT = 0.0f;
        Vec2  wanderDir{1.0f, 0.0f};
        float wanderCd = 0.0f;
    };

    GameContext* m_ctx     = nullptr;
    int          m_roomId  = 1;
    CardinalDir  m_entrySide = CardinalDir::S;
    TilemapAsset m_asset;
    std::vector<uint8_t> m_coll;
    int          m_mapW = 0, m_mapH = 0;

    Camera2D m_camera;

    Vec2   m_playerPos{0.0f, 0.0f};
    float  m_playerFacingX = 1.0f;     // -1/+1; still used for sword swing
    Facing m_facing        = Facing::S;
    float  m_walkAnimT     = 0.0f;     // seconds; resets to 0 when idle
    bool   m_moving        = false;
    float  m_playerHurt    = 0.0f;

    float m_swingT      = 0.0f;
    float m_swingDirX   = 1.0f;
    std::vector<uint8_t> m_swingHit;

    std::vector<Slime> m_slimes;
    std::vector<Vec2>  m_keys;
    std::vector<Vec2>  m_treasures;

    AssetWatcher m_watcher;
    bool m_watcherInit  = false;
    bool m_reloadPending = false;

    std::mt19937 m_rng;
};

// ---------------------------------------------------------------------------
// Application.
// ---------------------------------------------------------------------------
class ZeldaApp : public Application {
public:
    ZeldaApp() : Application(MakeConfig()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;
        m_font = LoadMSDFFont(GetDevice(), assets + "/msdf-inter.json");
        if (!m_font.Valid()) {
            NARAGAME_ERROR("zelda: missing %s/msdf-inter.json", assets.c_str());
        }

        m_ctx.renderer = &m_renderer;
        m_ctx.font     = &m_font;
        m_ctx.device   = &GetDevice();
        m_ctx.charsTex = LoadTexture(GetDevice(),
                                     assets + "/characters.png",
                                     TextureFilter::Point);
        if (m_ctx.charsTex) {
            m_ctx.charSheet = SpriteSheet(m_ctx.charsTex.get(), 32, 32);
        }
        m_ctx.linkTex = LoadTexture(GetDevice(),
                                    assets + "/link.png",
                                    TextureFilter::Point);
        if (!m_ctx.linkTex) {
            NARAGAME_WARN("zelda: missing %s/link.png -- player will be a quad",
                          assets.c_str());
        }

        m_ctx.makeTitle    = [this] { return std::make_unique<TitleScene>(&m_ctx); };
        m_ctx.makeRoom     = [this](int id, CardinalDir side) {
            return std::make_unique<RoomScene>(&m_ctx, id, side);
        };
        m_ctx.makeGameOver = [this](bool won) {
            return std::make_unique<GameOverScene>(&m_ctx, won);
        };
        m_scenes.Change(m_ctx.makeTitle());
    }

    void OnUpdate(float dt) override { m_scenes.Update(dt); }

    void OnRender() override {
        Renderer2D& r = *m_ctx.renderer;
        m_scenes.Render();
        if (!m_scenes.IsTransitioning()) return;
        r.Begin(Renderer2D::ScreenCamera(kScreenW, kScreenH));
        const TransitionKind k = m_scenes.TransitionType();
        if (k == TransitionKind::Fade) {
            Color c = m_scenes.TransitionColor();
            c.a = m_scenes.FadeAlpha();
            r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH}, c);
        }
        r.End();
    }

    void OnStop() override {
        m_ctx.charsTex.reset();
        m_ctx.linkTex.reset();
        m_scenes.Clear();
        m_renderer.Shutdown();
    }

private:
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title  = "Nara Quest";
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

} // namespace

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new ZeldaApp();
}
