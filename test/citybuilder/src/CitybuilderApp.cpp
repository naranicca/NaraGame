// Citybuilder — iso city-builder prototype, now built on engine IsoTerrain.
//
// The corner-height grid, slope-bit lookup, 1-step cascade, iso projection
// math, point-in-quad hit-testing, and Lambert slope shading all live in
// NaraGame::IsoTerrain. This file is the GAME on top of it:
//
//   - render branches per cell type (water / beach / land)
//   - elevation-aware face picker that clamps water-cell corners to sea level
//   - placeable 1x1 / 2x2 buildings drawn in painter order alongside terrain
//   - SC2K-style terraforming via LMB / RMB on the picked corner
//   - Camera2D-driven zoom + pan (no fixed origin)
//
// Controls:
//   Left  click       raise corner nearest cursor       (LMB digs above 0 -> land)
//   Right click       lower corner                       (RMB digs below 0 -> water)
//   Middle drag       pan camera
//   Mouse wheel       zoom in/out around the cursor
//   Arrow keys        pan camera (alt)
//   B / V             place 1x1 / 2x2 building at hovered cell
//   X                 remove any building containing the hovered cell
//   R                 toggle ROAD mode. In road mode, LMB-drag draws roads
//                     on every flat-above-water cell the cursor crosses;
//                     RMB-drag erases. Terraform LMB/RMB are suppressed
//                     while in road mode (so clicks while drawing don't
//                     reshape the land underneath).
//   S / L             save / load the whole world (terrain + roads + buildings
//                     + camera) to ./citybuilder.save in plain text.
//   ESC               quit

#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/BitmapFont.h>
#include <NaraGame/renderer/Camera2D.h>
#include <NaraGame/renderer/IsoTerrain.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/SpriteSheet.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

// 32x32 cell grid — big enough that you have to pan or zoom out to see it all
// in a 1024x640 viewport, which is the point of a milestone with a camera.
constexpr IsoTerrainConfig kTerrainCfg{
    /*gridW*/ 32, /*gridH*/ 32,
    /*tileW*/ 64.0f, /*tileH*/ 32.0f,
    /*stepHeight*/ 8,
    /*minHeight*/ -4,
    /*maxHeight*/  8,
};

constexpr float kSpriteW       = 64.0f;
constexpr float kSpriteH       = 40.0f;
constexpr float kSpriteAnchorY = 23.0f;

constexpr float kZoomMin = 0.25f;
constexpr float kZoomMax = 4.0f;
constexpr float kZoomStep = 1.15f;       // multiplier per wheel notch
constexpr float kArrowPanPxPerSec = 500.0f;

// Building textures: 64x64 red iso box for 1x1, 128x128 blue iso box for 2x2.
// Each sprite bakes three-face shading (top base, left 70%, right 50%) so they
// look 3D without a per-pixel light pass at draw time.
constexpr float kBuilding1Size = 64.0f;
constexpr float kBuilding2Size = 128.0f;

struct Building {
    int x, y;   // origin (back-most cell in footprint)
    int w, h;   // footprint size in cells (>=1)
};

bool BuildingContains(const Building& b, int cellX, int cellY) {
    return cellX >= b.x && cellX < b.x + b.w
        && cellY >= b.y && cellY < b.y + b.h;
}

} // namespace

class CitybuilderApp : public Application {
public:
    CitybuilderApp() : Application(MakeConfig()) {
        m_terrain.Init(kTerrainCfg);
    }

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;
        m_slopesTexture = LoadTexture(GetDevice(), assets + "/slopes.png");
        m_slopes = SpriteSheet(m_slopesTexture.get(), 64, 40);
        m_roadsTexture = LoadTexture(GetDevice(), assets + "/roads.png");
        m_roadsSheet   = SpriteSheet(m_roadsTexture.get(), 64, 40);
        m_roadCells.assign(static_cast<size_t>(m_terrain.Width()) * m_terrain.Height(), false);

        m_fontTexture = LoadTexture(GetDevice(), assets + "/font.png");
        m_font = BitmapFont(m_fontTexture.get(), ' ', 16, 14, 24);
        m_font.SetTracking(-2);

        m_building1x1Texture = LoadTexture(GetDevice(), assets + "/building_1x1.png");
        m_building1x1Sheet   = SpriteSheet(m_building1x1Texture.get(), 64, 64);
        m_building2x2Texture = LoadTexture(GetDevice(), assets + "/building_2x2.png");
        m_building2x2Sheet   = SpriteSheet(m_building2x2Texture.get(), 128, 128);

        m_camera.SetViewport(static_cast<float>(Config().width),
                             static_cast<float>(Config().height));
        m_camera.SetZoom(1.0f);
        // Center on the grid's vertical midpoint at world x = 0 — cells extend
        // symmetrically in x because cell (0, 0) sits at world origin, and the
        // iso projection drives x by (cellX - cellY) * halfW.
        const float gridVertCenter = (m_terrain.Width() + m_terrain.Height() - 2)
                                     * m_terrain.HalfH() * 0.5f;
        m_camera.CenterOn({0.0f, gridVertCenter});

        // Prefer the packaged demo map (procedurally generated; ships with
        // the assets folder) so the opening screen shows a richer world than
        // the bare seeded lake. Fall back to the seed if the file is missing.
        if (!LoadFrom(assets + "/demo.save"))
            SeedDemoLake();
        NARAGAME_INFO("Citybuilder: LMB/RMB terraform, MMB-drag pan, wheel zoom, B/V place, X remove, ESC quit");
    }

    void OnStop() override {
        m_slopesTexture.reset();
        m_roadsTexture.reset();
        m_fontTexture.reset();
        m_building1x1Texture.reset();
        m_building2x2Texture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        const Vec2 mouseScreen = Input::MousePosition();

        // --- Camera: zoom around the cursor. ---
        // Keep the world point currently under the cursor stationary: compute
        // it before changing zoom, then re-derive position so the same world
        // point still projects to the same screen pixel after the change.
        const float wheel = Input::MouseWheelDelta();
        if (wheel != 0.0f) {
            const float oldZoom = m_camera.Zoom();
            const Vec2 worldBefore = m_camera.Position() + mouseScreen / oldZoom;
            const float newZoom = Clamp(oldZoom * std::pow(kZoomStep, wheel),
                                        kZoomMin, kZoomMax);
            m_camera.SetZoom(newZoom);
            m_camera.SetPosition(worldBefore - mouseScreen / newZoom);
        }

        // --- Camera: pan. Middle-drag in screen pixels => world delta scaled
        // by 1/zoom. Arrow keys give the same effect at a fixed rate. ---
        if (Input::IsMouseDown(MouseButton::Middle)
            && !Input::IsMousePressed(MouseButton::Middle)) {
            const Vec2 delta = mouseScreen - m_lastMouseScreen;
            m_camera.SetPosition(m_camera.Position() - delta / m_camera.Zoom());
        }
        m_lastMouseScreen = mouseScreen;

        Vec2 panDir{0.0f, 0.0f};
        if (Input::IsKeyDown(Key::Left))  panDir.x -= 1.0f;
        if (Input::IsKeyDown(Key::Right)) panDir.x += 1.0f;
        if (Input::IsKeyDown(Key::Up))    panDir.y -= 1.0f;
        if (Input::IsKeyDown(Key::Down))  panDir.y += 1.0f;
        if (panDir.x != 0.0f || panDir.y != 0.0f) {
            const float len = std::sqrt(panDir.x * panDir.x + panDir.y * panDir.y);
            const Vec2 step = panDir
                * (kArrowPanPxPerSec * dt / (len * m_camera.Zoom()));
            m_camera.SetPosition(m_camera.Position() + step);
        }

        // --- Picking: convert screen -> world, then face-pick in terrain. ---
        const Vec2 mouseWorld = m_camera.Position() + mouseScreen / m_camera.Zoom();
        const PickResult pick = Pick(mouseWorld.x, mouseWorld.y);
        m_hoverCornerValid = pick.valid;
        m_hoverI     = pick.cornerI;
        m_hoverJ     = pick.cornerJ;
        m_hoverCellX = pick.cellX;
        m_hoverCellY = pick.cellY;

        // R always toggles road mode, regardless of cursor location.
        if (Input::IsKeyPressed(Key::R))
            m_roadMode = !m_roadMode;

        // Save / load work in any mode and don't need a hover. Bind to
        // letters because the engine Key enum doesn't include function keys.
        if (Input::IsKeyPressed(Key::S))
            Save();
        if (Input::IsKeyPressed(Key::L))
            Load();

        // Don't act on the same frame a middle-pan started, so accidental
        // clicks while panning don't fire.
        if (pick.valid && !Input::IsMouseDown(MouseButton::Middle)) {
            if (m_roadMode) {
                // Road draw / erase. IsMouseDown (not Pressed) so a held drag
                // applies on every frame the cursor enters a new cell.
                if (Input::IsMouseDown(MouseButton::Left))  TryPlaceRoad(m_hoverCellX, m_hoverCellY);
                if (Input::IsMouseDown(MouseButton::Right)) SetRoad(m_hoverCellX, m_hoverCellY, false);
            } else {
                // Terraform — edge-triggered so a held click only fires once.
                if (Input::IsMousePressed(MouseButton::Left))  m_terrain.AdjustCorner(m_hoverI, m_hoverJ, +1);
                if (Input::IsMousePressed(MouseButton::Right)) m_terrain.AdjustCorner(m_hoverI, m_hoverJ, -1);
            }

            // Building keys are mode-independent.
            if (Input::IsKeyPressed(Key::B))
                PlaceBuilding(m_hoverCellX, m_hoverCellY, 1, 1);
            if (Input::IsKeyPressed(Key::V)
                && m_hoverCellX <= m_terrain.Width()  - 2
                && m_hoverCellY <= m_terrain.Height() - 2)
                PlaceBuilding(m_hoverCellX, m_hoverCellY, 2, 2);
            if (Input::IsKeyPressed(Key::X))
                RemoveBuildingAt(m_hoverCellX, m_hoverCellY);
        }
    }

    void OnRender() override {
        GetDevice().Clear(Color(0.08f, 0.10f, 0.14f, 1.0f));
        m_renderer.Begin(m_camera.ViewProjection());

        // Diagonal back-to-front pass. After each diagonal's terrain cells,
        // any building whose front-most footprint cell sits on the same
        // diagonal slips in — its sprite covers its own footprint but is
        // covered by cells at higher x+y that visually overlap it.
        for (int sum = 0; sum <= m_terrain.MaxDiagonalSum(); ++sum) {
            const int xMin = m_terrain.DiagonalXMin(sum);
            const int xMax = m_terrain.DiagonalXMax(sum);
            for (int x = xMin; x <= xMax; ++x) {
                const int y = sum - x;
                DrawCell(x, y);
                if (HasRoad(x, y)) DrawRoad(x, y);
            }
            for (const Building& b : m_buildings)
                if ((b.x + b.w - 1) + (b.y + b.h - 1) == sum)
                    DrawBuilding(b);
        }

        if (m_hoverCornerValid) {
            // Clamp to sea level so underwater corners stay visible at the
            // water surface. Marker scales with zoom so it stays the same
            // pixel size on screen regardless of how far in/out you are.
            const int hh = Max(0, m_terrain.Corner(m_hoverI, m_hoverJ));
            const Vec2 p = m_terrain.CornerToScreen(m_hoverI, m_hoverJ, hh);
            const float kScreenMarker = 8.0f;
            const float marker = kScreenMarker / m_camera.Zoom();
            m_renderer.DrawQuad({ p.x - marker * 0.5f, p.y - marker * 0.5f },
                                { marker, marker },
                                Color(1.0f, 0.95f, 0.30f, 1.0f));
        }

        m_renderer.End();

        // HUD pass in screen space — second Begin/End so the text uses
        // window pixels regardless of camera zoom or pan.
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        m_renderer.Begin(Renderer2D::ScreenCamera(w, h));
        DrawHud();
        m_renderer.End();
    }

private:
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title = "NaraGame - Citybuilder";
        c.width = 1024;
        c.height = 640;
        return c;
    }

    struct PickResult {
        bool valid    = false;
        int  cellX    = -1;
        int  cellY    = -1;
        int  cornerI  = -1;
        int  cornerJ  = -1;
    };

    // Same picker as before, just in world coords (no kOrigin offset). Caller
    // converts screen->world via the camera before calling.
    PickResult Pick(float mx, float my) const {
        PickResult r;
        int bestKey = -1;
        const Vec2 mouse{mx, my};
        for (int y = 0; y < m_terrain.Height(); ++y) {
            for (int x = 0; x < m_terrain.Width(); ++x) {
                const IsoCellSlope s = m_terrain.CellSlope(x, y);
                const bool water = s.maxH < 0;
                const int hT = m_terrain.Corner(x,     y    );
                const int hR = m_terrain.Corner(x + 1, y    );
                const int hB = m_terrain.Corner(x + 1, y + 1);
                const int hL = m_terrain.Corner(x,     y + 1);
                const int rT = water ? 0 : hT;
                const int rR = water ? 0 : hR;
                const int rB = water ? 0 : hB;
                const int rL = water ? 0 : hL;
                const Vec2 sT = m_terrain.CornerToScreen(x,     y,     rT);
                const Vec2 sR = m_terrain.CornerToScreen(x + 1, y,     rR);
                const Vec2 sB = m_terrain.CornerToScreen(x + 1, y + 1, rB);
                const Vec2 sL = m_terrain.CornerToScreen(x,     y + 1, rL);
                if (!PointInQuad(mouse, sT, sR, sB, sL))
                    continue;
                const int renderElev = water ? 0 : s.baseElev;
                const int key = (x + y) * m_terrain.HeightRange()
                              + (renderElev - m_terrain.MinHeight());
                if (key <= bestKey)
                    continue;
                bestKey = key;
                r.valid = true;
                r.cellX = x;
                r.cellY = y;
                const Vec2 cs[4] = { sT, sR, sB, sL };
                const int ci[4][2] = {
                    {x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}
                };
                float bestD = std::numeric_limits<float>::max();
                int   bestC = 0;
                for (int k = 0; k < 4; ++k) {
                    const float ddx = cs[k].x - mx;
                    const float ddy = cs[k].y - my;
                    const float d = ddx * ddx + ddy * ddy;
                    if (d < bestD) { bestD = d; bestC = k; }
                }
                r.cornerI = ci[bestC][0];
                r.cornerJ = ci[bestC][1];
            }
        }
        return r;
    }

    void DrawCell(int x, int y) {
        const IsoCellSlope s = m_terrain.CellSlope(x, y);
        const bool darkCheck = ((x + y) & 1) == 0;
        const Vec2 center = m_terrain.TileCenterToScreen(x, y);

        if (s.maxH < 0) {
            const Vec2 topLeft{ center.x - kSpriteW * 0.5f, center.y - kSpriteAnchorY };
            const Color tint = darkCheck
                ? Color(0.18f, 0.40f, 0.66f, 1.0f)
                : Color(0.22f, 0.46f, 0.72f, 1.0f);
            m_renderer.DrawQuad(topLeft, {kSpriteW, kSpriteH}, m_slopes.At(0), tint);
            return;
        }

        const Vec2 topLeft{
            center.x - kSpriteW * 0.5f,
            center.y - kSpriteAnchorY - static_cast<float>(s.baseElev * m_terrain.StepHeight())
        };

        const float ev = static_cast<float>(s.baseElev);
        Color tint;
        if (s.minH < 0) {
            tint = darkCheck
                ? Color(0.62f + 0.04f * ev, 0.55f + 0.04f * ev, 0.32f + 0.04f * ev, 1.0f)
                : Color(0.72f + 0.04f * ev, 0.64f + 0.04f * ev, 0.40f + 0.04f * ev, 1.0f);
        } else {
            tint = darkCheck
                ? Color(0.40f + 0.06f * ev, 0.62f + 0.06f * ev, 0.30f + 0.06f * ev, 1.0f)
                : Color(0.50f + 0.06f * ev, 0.74f + 0.06f * ev, 0.38f + 0.06f * ev, 1.0f);
        }
        const float b = SlopeBrightness(s.slopeBits);
        tint.r = Clamp(tint.r * b, 0.0f, 1.0f);
        tint.g = Clamp(tint.g * b, 0.0f, 1.0f);
        tint.b = Clamp(tint.b * b, 0.0f, 1.0f);

        m_renderer.DrawQuad(topLeft, {kSpriteW, kSpriteH}, m_slopes.At(s.slopeBits), tint);
    }

    int FootprintBaseElev(const Building& b) const {
        int e = INT_MAX;
        for (int j = b.y; j <= b.y + b.h; ++j)
            for (int i = b.x; i <= b.x + b.w; ++i)
                e = Min(e, m_terrain.Corner(i, j));
        return e == INT_MAX ? 0 : e;
    }

    void DrawBuilding(const Building& b) {
        // Footprint centroid in tile space -> world (caller-relative-to-origin).
        const float mtx = b.x + (b.w - 1) * 0.5f;
        const float mty = b.y + (b.h - 1) * 0.5f;
        const Vec2 ground{
            (mtx - mty) * m_terrain.HalfW(),
            (mtx + mty) * m_terrain.HalfH()
        };
        const int baseElev = FootprintBaseElev(b);
        const float screenCy = ground.y - static_cast<float>(baseElev * m_terrain.StepHeight());

        // Anchor: sprite's bottom-center (where the box's south-ground corner
        // sits) aligns with the centroid's south corner. For a wxh footprint
        // the south corner is centroid + (0, halfDiamondH) where halfDiamondH
        // grows with the footprint: 1x1 -> halfH (16 px), 2x2 -> 2*halfH (32).
        SpriteSheet* sheet = nullptr;
        float spriteSize = 0.0f;
        float halfDiamondH = 0.0f;
        if (b.w == 1 && b.h == 1) {
            sheet = &m_building1x1Sheet;
            spriteSize = kBuilding1Size;
            halfDiamondH = m_terrain.HalfH();          // 16 px
        } else if (b.w == 2 && b.h == 2) {
            sheet = &m_building2x2Sheet;
            spriteSize = kBuilding2Size;
            halfDiamondH = m_terrain.HalfH() * 2.0f;   // 32 px
        } else {
            return; // unsupported footprint for prototype
        }

        const Vec2 topLeft{
            ground.x - spriteSize * 0.5f,
            screenCy - spriteSize + halfDiamondH
        };
        m_renderer.DrawQuad(topLeft, {spriteSize, spriteSize},
                            sheet->At(0), Color(1.0f, 1.0f, 1.0f, 1.0f));
    }

    void PlaceBuilding(int x, int y, int w, int h) {
        for (const Building& b : m_buildings) {
            const bool overlapX = !(x + w <= b.x || b.x + b.w <= x);
            const bool overlapY = !(y + h <= b.y || b.y + b.h <= y);
            if (overlapX && overlapY) return;
        }
        m_buildings.push_back({x, y, w, h});
    }

    void RemoveBuildingAt(int cellX, int cellY) {
        m_buildings.erase(
            std::remove_if(m_buildings.begin(), m_buildings.end(),
                           [cellX, cellY](const Building& b) {
                               return BuildingContains(b, cellX, cellY);
                           }),
            m_buildings.end());
    }

    bool HasRoad(int x, int y) const {
        return m_roadCells[static_cast<size_t>(y) * m_terrain.Width() + x];
    }

    void SetRoad(int x, int y, bool v) {
        m_roadCells[static_cast<size_t>(y) * m_terrain.Width() + x] = v;
    }

    // Idempotent road placement. No-op if a road is already here; otherwise
    // place it only when the cell is flat and at or above sea level — a
    // sloped cell would render a flat road floating off the actual surface.
    // (Separate from removal so LMB-drag and RMB-drag never collide.)
    void TryPlaceRoad(int x, int y) {
        if (HasRoad(x, y)) return;
        const IsoCellSlope s = m_terrain.CellSlope(x, y);
        const bool flat = (s.maxH == s.minH);
        const bool aboveWater = s.minH >= 0;
        if (flat && aboveWater)
            SetRoad(x, y, true);
    }

    // 4-bit cardinal-neighbour bitmask used to pick a sprite from roads.png.
    // bit 0 = N (y-1), 1 = E (x+1), 2 = S (y+1), 3 = W (x-1). Computed fresh
    // every frame from m_roadCells so placements auto-connect without bookkeeping.
    int RoadMask(int x, int y) const {
        int mask = 0;
        if (y > 0                          && HasRoad(x,     y - 1)) mask |= 1;
        if (x < m_terrain.Width()  - 1     && HasRoad(x + 1, y    )) mask |= 2;
        if (y < m_terrain.Height() - 1     && HasRoad(x,     y + 1)) mask |= 4;
        if (x > 0                          && HasRoad(x - 1, y    )) mask |= 8;
        return mask;
    }

    void DrawRoad(int x, int y) {
        const IsoCellSlope s = m_terrain.CellSlope(x, y);
        const Vec2 center = m_terrain.TileCenterToScreen(x, y);
        const Vec2 topLeft{
            center.x - kSpriteW * 0.5f,
            center.y - kSpriteAnchorY - static_cast<float>(s.baseElev * m_terrain.StepHeight())
        };
        m_renderer.DrawQuad(topLeft, {kSpriteW, kSpriteH},
                            m_roadsSheet.At(RoadMask(x, y)),
                            Color(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // Top-left text overlay: title + live hover/camera state + controls help.
    // Called from a separate Begin/End in OnRender that uses screen-pixel
    // coords, so the layout doesn't have to undo the camera transform.
    void DrawHud() {
        constexpr float kPad = 12.0f;
        constexpr float kScale = 1.0f;
        const float lineH = static_cast<float>(m_font.LineHeight()) * kScale;
        const Color white {1.00f, 1.00f, 1.00f, 1.0f};
        const Color dim   {0.78f, 0.80f, 0.85f, 1.0f};
        const Color dimmer{0.55f, 0.57f, 0.62f, 1.0f};

        float y = kPad;
        auto line = [&](const std::string& s, const Color& c) {
            m_renderer.DrawText(m_font, s, {kPad, y}, c, kScale);
            y += lineH;
        };

        line("CITYBUILDER", white);

        // Mode line right under the title — yellow when the user is in road
        // mode so it's hard to miss that LMB no longer terraforms.
        const Color modeColor = m_roadMode ? Color{1.0f, 0.85f, 0.30f, 1.0f} : white;
        line(m_roadMode ? "Mode  ROAD (LMB draw, RMB erase)"
                        : "Mode  TERRAFORM (LMB raise, RMB lower)",
             modeColor);

        char buf[96];
        if (m_hoverCornerValid) {
            std::snprintf(buf, sizeof(buf), "Cell %d,%d  Corner %d,%d h=%d",
                          m_hoverCellX, m_hoverCellY, m_hoverI, m_hoverJ,
                          m_terrain.Corner(m_hoverI, m_hoverJ));
        } else {
            std::snprintf(buf, sizeof(buf), "Cell  -");
        }
        line(buf, dim);

        int roadCount = 0;
        for (bool v : m_roadCells) if (v) ++roadCount;
        std::snprintf(buf, sizeof(buf), "Roads %d   Buildings %d",
                      roadCount, static_cast<int>(m_buildings.size()));
        line(buf, dim);

        std::snprintf(buf, sizeof(buf), "Zoom %.2fx", m_camera.Zoom());
        line(buf, dim);

        y += lineH * 0.4f;
        line("R  toggle road mode", dimmer);
        line("MMB drag pan  Wheel zoom", dimmer);
        line("Arrows pan", dimmer);
        line("B 1x1  V 2x2  X del", dimmer);
        line("S save   L load   ESC quit", dimmer);
    }

    // ----------------------------------------------------------------------
    // Persistence: a single plain-text dump of every authored bit of state
    // (terrain corners, road bits, building list, camera). Plain text so the
    // file is readable in a normal editor — a tiny prototype world serialises
    // to a few KB and the debuggability is worth more than the bytes saved.
    // Section order is fixed by the writer; the reader tolerates any order
    // (handy when hand-editing) and skips unknown tokens (forward-compat).
    // ----------------------------------------------------------------------
    static constexpr const char* kSaveFile = "citybuilder.save";

    void Save() {
        std::ofstream out(kSaveFile);
        if (!out) {
            NARAGAME_WARN("citybuilder: failed to open %s for write", kSaveFile);
            return;
        }
        out << "NaraSave 1\n";
        out << "grid "
            << m_terrain.Width()  << ' ' << m_terrain.Height() << ' '
            << m_terrain.MinHeight() << ' ' << m_terrain.MaxHeight() << ' '
            << m_terrain.StepHeight() << '\n';
        const Vec2 cp = m_camera.Position();
        out << "camera " << cp.x << ' ' << cp.y << ' ' << m_camera.Zoom() << '\n';

        out << "corners\n";
        for (int j = 0; j < m_terrain.CornerRows(); ++j)
            for (int i = 0; i < m_terrain.CornerCols(); ++i)
                out << m_terrain.Corner(i, j) << (i + 1 == m_terrain.CornerCols() ? '\n' : ' ');

        out << "roads\n";
        for (int y = 0; y < m_terrain.Height(); ++y)
            for (int x = 0; x < m_terrain.Width(); ++x)
                out << (HasRoad(x, y) ? '1' : '0') << (x + 1 == m_terrain.Width() ? '\n' : ' ');

        out << "buildings " << m_buildings.size() << '\n';
        for (const Building& b : m_buildings)
            out << b.x << ' ' << b.y << ' ' << b.w << ' ' << b.h << '\n';

        out << "end\n";
        NARAGAME_INFO("citybuilder: saved -> %s", kSaveFile);
    }

    // Wrapper: S key uses this for "load the user's last save".
    void Load() {
        if (!LoadFrom(kSaveFile))
            NARAGAME_WARN("citybuilder: %s not found", kSaveFile);
    }

    // Returns true on success, false if the file is missing or has the wrong
    // header. Used both by the user-driven Load() and by OnStart to load the
    // packaged demo map.
    bool LoadFrom(const std::string& path) {
        std::ifstream in(path);
        if (!in) return false;
        std::string token;
        in >> token;
        int version = 0;
        in >> version;
        if (token != "NaraSave" || version != 1) {
            NARAGAME_WARN("citybuilder: %s has wrong header", path.c_str());
            return false;
        }

        while (in >> token) {
            if (token == "grid") {
                int w, h, mn, mx, step;
                in >> w >> h >> mn >> mx >> step;
                if (w != m_terrain.Width() || h != m_terrain.Height())
                    NARAGAME_WARN("citybuilder: save grid %dx%d differs from app %dx%d",
                                  w, h, m_terrain.Width(), m_terrain.Height());
            } else if (token == "camera") {
                float x, y, z;
                in >> x >> y >> z;
                m_camera.SetPosition({x, y});
                m_camera.SetZoom(z);
            } else if (token == "corners") {
                for (int j = 0; j < m_terrain.CornerRows(); ++j)
                    for (int i = 0; i < m_terrain.CornerCols(); ++i) {
                        int v; in >> v;
                        m_terrain.SetCorner(i, j, v);
                    }
            } else if (token == "roads") {
                for (int y = 0; y < m_terrain.Height(); ++y)
                    for (int x = 0; x < m_terrain.Width(); ++x) {
                        int v; in >> v;
                        SetRoad(x, y, v != 0);
                    }
            } else if (token == "buildings") {
                int n; in >> n;
                m_buildings.clear();
                m_buildings.reserve(static_cast<size_t>(n));
                for (int i = 0; i < n; ++i) {
                    Building b{};
                    in >> b.x >> b.y >> b.w >> b.h;
                    m_buildings.push_back(b);
                }
            } else if (token == "end") {
                break;
            } else {
                std::string rest;
                std::getline(in, rest);
            }
        }
        NARAGAME_INFO("citybuilder: loaded <- %s", path.c_str());
        return true;
    }

    void SeedDemoLake() {
        // Same lake as before, but on the bigger 32x32 grid it now sits in
        // the upper-left quadrant — pan/zoom to see other empty regions.
        for (int j = 12; j <= 16; ++j)
            for (int i = 12; i <= 16; ++i)
                m_terrain.SetCorner(i, j, -2);
        for (int j = 11; j <= 16; ++j)
            for (int i = 11; i <= 16; ++i)
                if (m_terrain.Corner(i, j) == 0)
                    m_terrain.SetCorner(i, j, -1);
    }

    Renderer2D                  m_renderer;
    std::unique_ptr<RHITexture> m_slopesTexture;
    SpriteSheet                 m_slopes;
    std::unique_ptr<RHITexture> m_roadsTexture;
    SpriteSheet                 m_roadsSheet;
    std::vector<bool>           m_roadCells;
    std::unique_ptr<RHITexture> m_fontTexture;
    BitmapFont                  m_font;
    std::unique_ptr<RHITexture> m_building1x1Texture;
    SpriteSheet                 m_building1x1Sheet;
    std::unique_ptr<RHITexture> m_building2x2Texture;
    SpriteSheet                 m_building2x2Sheet;
    IsoTerrain                  m_terrain;
    std::vector<Building>       m_buildings;
    Camera2D                    m_camera;

    Vec2 m_lastMouseScreen{0.0f, 0.0f};
    int  m_hoverI = -1, m_hoverJ = -1;
    bool m_hoverCornerValid = false;
    int  m_hoverCellX = -1, m_hoverCellY = -1;
    bool m_roadMode = false;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CitybuilderApp();
}
