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
//   R                 toggle road on hovered cell (flat + above water only;
//                     16-variant sprite auto-connects to neighbouring roads)
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

const Color kColor1x1{0.86f, 0.34f, 0.22f, 1.0f};
const Color kColor2x2{0.30f, 0.50f, 0.85f, 1.0f};

struct Building {
    int   x, y;
    int   w, h;
    Color color;
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

        m_camera.SetViewport(static_cast<float>(Config().width),
                             static_cast<float>(Config().height));
        m_camera.SetZoom(1.0f);
        // Center on the grid's vertical midpoint at world x = 0 — cells extend
        // symmetrically in x because cell (0, 0) sits at world origin, and the
        // iso projection drives x by (cellX - cellY) * halfW.
        const float gridVertCenter = (m_terrain.Width() + m_terrain.Height() - 2)
                                     * m_terrain.HalfH() * 0.5f;
        m_camera.CenterOn({0.0f, gridVertCenter});

        SeedDemoLake();
        NARAGAME_INFO("Citybuilder: LMB/RMB terraform, MMB-drag pan, wheel zoom, B/V place, X remove, ESC quit");
    }

    void OnStop() override {
        m_slopesTexture.reset();
        m_roadsTexture.reset();
        m_fontTexture.reset();
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

        // Don't terraform on the same frame a middle-pan started, so a quick
        // accidental left-click while panning doesn't fire.
        if (pick.valid && !Input::IsMouseDown(MouseButton::Middle)) {
            if (Input::IsMousePressed(MouseButton::Left))
                m_terrain.AdjustCorner(m_hoverI, m_hoverJ, +1);
            if (Input::IsMousePressed(MouseButton::Right))
                m_terrain.AdjustCorner(m_hoverI, m_hoverJ, -1);

            if (Input::IsKeyPressed(Key::B))
                PlaceBuilding(m_hoverCellX, m_hoverCellY, 1, 1, kColor1x1);
            if (Input::IsKeyPressed(Key::V)
                && m_hoverCellX <= m_terrain.Width()  - 2
                && m_hoverCellY <= m_terrain.Height() - 2)
                PlaceBuilding(m_hoverCellX, m_hoverCellY, 2, 2, kColor2x2);
            if (Input::IsKeyPressed(Key::X))
                RemoveBuildingAt(m_hoverCellX, m_hoverCellY);
            if (Input::IsKeyPressed(Key::R))
                ToggleRoad(m_hoverCellX, m_hoverCellY);
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
        const float mtx = b.x + (b.w - 1) * 0.5f;
        const float mty = b.y + (b.h - 1) * 0.5f;
        const Vec2 ground{
            (mtx - mty) * m_terrain.HalfW(),
            (mtx + mty) * m_terrain.HalfH()
        };
        const int baseElev = FootprintBaseElev(b);
        const float screenBottomY = ground.y - static_cast<float>(baseElev * m_terrain.StepHeight());

        float boxW = 0.0f, boxH = 0.0f;
        if      (b.w == 1 && b.h == 1) { boxW = 30.0f; boxH = 36.0f; }
        else if (b.w == 2 && b.h == 2) { boxW = 72.0f; boxH = 60.0f; }
        else                           { boxW = 24.0f * (b.w + b.h); boxH = 28.0f * Max(b.w, b.h); }

        m_renderer.DrawQuad({ ground.x - boxW * 0.5f, screenBottomY - boxH },
                            { boxW, boxH }, b.color);
    }

    void PlaceBuilding(int x, int y, int w, int h, const Color& color) {
        for (const Building& b : m_buildings) {
            const bool overlapX = !(x + w <= b.x || b.x + b.w <= x);
            const bool overlapY = !(y + h <= b.y || b.y + b.h <= y);
            if (overlapX && overlapY) return;
        }
        m_buildings.push_back({x, y, w, h, color});
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

    // Toggle road on a cell. Reject placement on non-flat or underwater cells —
    // the road sprite is a 64x40 flat-diamond decal anchored at baseElev, so a
    // sloped cell would render a flat road floating off the actual surface.
    void ToggleRoad(int x, int y) {
        if (HasRoad(x, y)) { SetRoad(x, y, false); return; }
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
        line("LMB raise  RMB lower", dimmer);
        line("MMB drag pan  Wheel zoom", dimmer);
        line("Arrows pan", dimmer);
        line("B 1x1  V 2x2  X del  R road", dimmer);
        line("ESC quit", dimmer);
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
    IsoTerrain                  m_terrain;
    std::vector<Building>       m_buildings;
    Camera2D                    m_camera;

    Vec2 m_lastMouseScreen{0.0f, 0.0f};
    int  m_hoverI = -1, m_hoverJ = -1;
    bool m_hoverCornerValid = false;
    int  m_hoverCellX = -1, m_hoverCellY = -1;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CitybuilderApp();
}
