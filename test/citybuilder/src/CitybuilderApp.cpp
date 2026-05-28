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
//
// Controls:
//   Left click    raise corner nearest cursor      (RMB digs below 0 -> water)
//   Right click   lower corner
//   B             place 1x1 building at hovered cell
//   V             place 2x2 building (origin = hovered cell, +x +y)
//   X             remove any building containing the hovered cell
//   ESC           quit

#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/IsoTerrain.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/SpriteSheet.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <algorithm>
#include <climits>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

// Terrain configuration. The actual heightfield + slope math lives in
// IsoTerrain; this file just decides how to look at it.
constexpr IsoTerrainConfig kTerrainCfg{
    /*gridW*/ 16, /*gridH*/ 16,
    /*tileW*/ 64.0f, /*tileH*/ 32.0f,
    /*stepHeight*/ 8,
    /*minHeight*/ -4,   // negative depths for water
    /*maxHeight*/  8,
};

// Slope sprites are taller than the diamond (extra 8 px on top for raised
// corners). Their anchor — the y offset from sprite-local origin down to the
// flat-diamond CENTER — is half the diamond height plus the 8 px headroom.
constexpr float kSpriteW       = 64.0f;
constexpr float kSpriteH       = 40.0f;
constexpr float kSpriteAnchorY = 23.0f;

// Where to place the (0, 0) cell on screen. The terrain helpers return
// iso-relative coordinates; the game adds this offset.
constexpr Vec2 kOrigin{512.0f, 80.0f};

// Distinct prototype palettes per building size.
const Color kColor1x1{0.86f, 0.34f, 0.22f, 1.0f};
const Color kColor2x2{0.30f, 0.50f, 0.85f, 1.0f};

struct Building {
    int   x, y;   // origin (back-most cell in footprint)
    int   w, h;   // footprint size in cells (>=1)
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
        SeedDemoLake();
        NARAGAME_INFO("Citybuilder: LMB/RMB terraform (RMB digs water), B/V place, X remove, ESC quit");
    }

    void OnStop() override {
        m_slopesTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float /*dt*/) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        const Vec2 mp = Input::MousePosition();
        const PickResult pick = Pick(mp.x, mp.y);
        m_hoverCornerValid = pick.valid;
        m_hoverI     = pick.cornerI;
        m_hoverJ     = pick.cornerJ;
        m_hoverCellX = pick.cellX;
        m_hoverCellY = pick.cellY;

        if (pick.valid) {
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
        }
    }

    void OnRender() override {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        GetDevice().Clear(Color(0.08f, 0.10f, 0.14f, 1.0f));
        m_renderer.Begin(Renderer2D::ScreenCamera(w, h));

        // Diagonal back-to-front pass. After each diagonal's terrain cells are
        // drawn, any building whose front-most footprint cell sits on that
        // diagonal slips in — its sprite covers its own footprint but is
        // covered by cells at higher x+y that visually overlap it.
        for (int sum = 0; sum <= m_terrain.MaxDiagonalSum(); ++sum) {
            const int xMin = m_terrain.DiagonalXMin(sum);
            const int xMax = m_terrain.DiagonalXMax(sum);
            for (int x = xMin; x <= xMax; ++x)
                DrawCell(x, sum - x);
            for (const Building& b : m_buildings)
                if ((b.x + b.w - 1) + (b.y + b.h - 1) == sum)
                    DrawBuilding(b);
        }

        // Hover marker on top of everything; clamped to sea level so under-
        // water corners' markers stay visible at the water surface rather
        // than disappearing beneath an opaque water tile.
        if (m_hoverCornerValid) {
            const int hh = Max(0, m_terrain.Corner(m_hoverI, m_hoverJ));
            const Vec2 p = kOrigin + m_terrain.CornerToScreen(m_hoverI, m_hoverJ, hh);
            constexpr float kMarker = 8.0f;
            m_renderer.DrawQuad({ p.x - kMarker * 0.5f, p.y - kMarker * 0.5f },
                                { kMarker, kMarker },
                                Color(1.0f, 0.95f, 0.30f, 1.0f));
        }

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

    // Elevation-aware face picker. For each cell, project the 4 corners with
    // their CURRENT render heights (water cells are rendered as flat at h=0,
    // so their corners are clamped), test point-in-quad, keep the camera-
    // frontmost hit (highest x+y; tiebreak by render elev). The picked corner
    // is the closest of the cell's 4 in screen space, also elevation-aware.
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
                const Vec2 sT = kOrigin + m_terrain.CornerToScreen(x,     y,     rT);
                const Vec2 sR = kOrigin + m_terrain.CornerToScreen(x + 1, y,     rR);
                const Vec2 sB = kOrigin + m_terrain.CornerToScreen(x + 1, y + 1, rB);
                const Vec2 sL = kOrigin + m_terrain.CornerToScreen(x,     y + 1, rL);
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
        const Vec2 center = kOrigin + m_terrain.TileCenterToScreen(x, y);

        // WATER cell: every corner below sea level. Render as a flat diamond
        // at h = 0 (the water surface).
        if (s.maxH < 0) {
            const Vec2 topLeft{ center.x - kSpriteW * 0.5f, center.y - kSpriteAnchorY };
            const Color tint = darkCheck
                ? Color(0.18f, 0.40f, 0.66f, 1.0f)
                : Color(0.22f, 0.46f, 0.72f, 1.0f);
            m_renderer.DrawQuad(topLeft, {kSpriteW, kSpriteH}, m_slopes.At(0), tint);
            return;
        }

        // BEACH or LAND — slope tile drawn at its actual baseElev (which may
        // be negative for beach tiles). The painter's sort by x+y means the
        // water tile in front naturally covers any submerged portion.
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

    // Lowest corner anywhere in a building's footprint. The building sits at
    // this elevation so it doesn't float above an uneven patch.
    int FootprintBaseElev(const Building& b) const {
        int e = INT_MAX;
        for (int j = b.y; j <= b.y + b.h; ++j)
            for (int i = b.x; i <= b.x + b.w; ++i)
                e = Min(e, m_terrain.Corner(i, j));
        return e == INT_MAX ? 0 : e;
    }

    void DrawBuilding(const Building& b) {
        // Footprint center in tile-space -> screen, then drop by baseElev.
        const float mtx = b.x + (b.w - 1) * 0.5f;
        const float mty = b.y + (b.h - 1) * 0.5f;
        const Vec2 ground = kOrigin + Vec2{
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
        // Reject overlap so a stray key press doesn't quietly stack two
        // sprites in the same footprint.
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

    // Seed a small lake in the SE corner so the game opens with visible
    // water + beach. The layout is constructed to satisfy the 1-step cascade
    // by construction; SetCorner writes raw heights without invoking it.
    void SeedDemoLake() {
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
    IsoTerrain                  m_terrain;
    std::vector<Building>       m_buildings;

    int  m_hoverI = -1, m_hoverJ = -1;
    bool m_hoverCornerValid = false;
    int  m_hoverCellX = -1, m_hoverCellY = -1;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CitybuilderApp();
}
