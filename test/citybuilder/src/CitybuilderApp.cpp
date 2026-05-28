// Citybuilder — milestone (e): elevation-aware face-based picking.
//
// Replaces the flat-plane hover / click projection with a CPU "which visible
// face is under the cursor" picker. For each cell we project its 4 corners
// with the current elevation, test point-in-quad against the cursor, then
// among hits keep the camera-frontmost one (highest x+y; tiebreak by baseElev).
// The picked corner is the closest of that cell's 4 corners in screen space.
// This is the spirit of SC2K-style "render IDs to an offscreen buffer and
// read the pixel under the cursor" — done in CPU because the engine doesn't
// have render-to-texture yet and 16x16 cells make the brute-force loop free.
//
// Effect: clicking on a visible hill peak now picks the peak corner; clicking
// on a slope face picks a corner of that face. The yellow hover marker
// likewise rides the visible terrain instead of the flat plane.
//
// Earlier milestones (carried forward unchanged):
//
// (a) flat diamond grid + ScreenToTile picking
// (b) corner-height grid + 16-variant slope atlas + diagonal painter sort
// (c) interactive raise/lower with 8-conn slope-limit cascade
// (d) multi-cell buildings sorted by front-most footprint cell's x+y
//
// Controls:
//   Left  click   raise corner nearest cursor (terraform)
//   Right click   lower corner
//   B             place 1x1 building at hovered cell
//   V             place 2x2 building (origin = hovered cell, extends +x, +y)
//   X             remove any building whose footprint contains the hovered cell
//   ESC           quit

#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/SpriteSheet.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

// Grid + iso projection (unchanged from earlier milestones).
constexpr int   kGridW   = 16;
constexpr int   kGridH   = 16;
constexpr float kTileW   = 64.0f;
constexpr float kTileH   = 32.0f;
constexpr float kHalfW   = kTileW * 0.5f;
constexpr float kHalfH   = kTileH * 0.5f;
constexpr float kOriginX = 512.0f;
constexpr float kOriginY = 80.0f;

constexpr float kSpriteW       = 64.0f;
constexpr float kSpriteH       = 40.0f;
constexpr float kSpriteAnchorY = 23.0f;
constexpr int   kStepHeight    = 8;

constexpr int kCornerCols = kGridW + 1;
constexpr int kCornerRows = kGridH + 1;

constexpr int kMaxHeight = 8;
constexpr int kMinHeight = 0;

Vec2 TileCenterToScreen(int tx, int ty) {
    return { (tx - ty) * kHalfW + kOriginX, (tx + ty) * kHalfH + kOriginY };
}

Vec2 CornerToScreen(int i, int j, int height) {
    return {
        (i - j) * kHalfW + kOriginX,
        (i + j - 1) * kHalfH + kOriginY - static_cast<float>(height * kStepHeight)
    };
}

// Triangle point-in test via consistent cross-product sign — robust against
// CW vs CCW winding because we accept "all same sign or all zero".
bool PointInTri(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    const float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    const float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    const float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNeg && hasPos);
}

// Convex-quad point-in test by splitting on the (a,c) diagonal. Slope tiles
// stay (close to) convex while the slope-limit cascade keeps neighbour
// heights within one step; the rare "saddle" mask (5 or 10) goes butterfly,
// which this test will under-report — acceptable for a prototype.
bool PointInQuad(Vec2 p, Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    return PointInTri(p, a, b, c) || PointInTri(p, a, c, d);
}

// Lambert shading for a slope mask. Compute the surface normal of the slope
// from the 4 corner heights, then dot with a fixed light direction from the
// upper-left (-1, -1, +1) — the standard SC2K convention. Returns a tint
// multiplier in roughly [0.75, 1.10]. Flat tiles get the same multiplier as a
// perfectly-up normal, so an entire flat plateau reads as one consistent
// brightness while slope tiles around it pop in/out as their faces turn
// toward / away from the light.
float SlopeBrightness(int mask) {
    const float hT = (mask & 1) ? 1.0f : 0.0f;
    const float hR = (mask & 2) ? 1.0f : 0.0f;
    const float hB = (mask & 4) ? 1.0f : 0.0f;
    const float hL = (mask & 8) ? 1.0f : 0.0f;
    // Surface normal as (north-vec) × (east-vec) where
    //   north-vec = (0, -2, hT - hB),  east-vec = (2, 0, hR - hL).
    // Cross product yields (-2*(hR-hL), 2*(hT-hB), 4).
    const float nx = -2.0f * (hR - hL);
    const float ny =  2.0f * (hT - hB);
    const float nz =  4.0f;
    const float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
    // Light direction toward source: upper-left in screen = NW + up in world.
    constexpr float lx = -1.0f, ly = -1.0f, lz = 1.0f;
    constexpr float lLen = 1.7320508f; // sqrt(3)
    const float dot = (nx * lx + ny * ly + nz * lz) / (nLen * lLen);
    // Ambient floor so even back-facing slopes stay visible; diffuse term
    // adds the directional component on top.
    return 0.78f + 0.32f * Max(0.0f, dot);
}

// Distinct prototype palettes by size so 1x1 vs 2x2 are obvious at a glance.
const Color kColor1x1{0.86f, 0.34f, 0.22f, 1.0f};
const Color kColor2x2{0.30f, 0.50f, 0.85f, 1.0f};

struct Building {
    int   x, y;   // origin (back-most cell in footprint)
    int   w, h;   // footprint size in cells (>=1)
    Color color;
};

// Footprint occupancy test.
bool BuildingContains(const Building& b, int cellX, int cellY) {
    return cellX >= b.x && cellX < b.x + b.w
        && cellY >= b.y && cellY < b.y + b.h;
}

} // namespace

class CitybuilderApp : public Application {
public:
    CitybuilderApp() : Application(MakeConfig()) {
        m_corners.assign(static_cast<size_t>(kCornerCols) * kCornerRows, 0);
    }

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;
        m_slopesTexture = LoadTexture(GetDevice(), assets + "/slopes.png");
        m_slopes = SpriteSheet(m_slopesTexture.get(), 64, 40);
        NARAGAME_INFO("Citybuilder: LMB/RMB terraform, B=1x1, V=2x2, X=remove, ESC=quit");
    }

    void OnStop() override {
        m_slopesTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float /*dt*/) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        // Single elevation-aware pick drives both corner hover (terraforming)
        // and cell hover (building placement). With this picker the marker
        // and clicks land on whatever the player visibly sees under the
        // cursor instead of on the flat-plane projection beneath it.
        const Vec2 mp = Input::MousePosition();
        const PickResult pick = Pick(mp.x, mp.y);
        m_hoverCornerValid = pick.valid;
        m_hoverI    = pick.cornerI;
        m_hoverJ    = pick.cornerJ;
        m_hoverCellX = pick.cellX;
        m_hoverCellY = pick.cellY;

        if (pick.valid) {
            if (Input::IsMousePressed(MouseButton::Left))  AdjustCorner(m_hoverI, m_hoverJ, +1);
            if (Input::IsMousePressed(MouseButton::Right)) AdjustCorner(m_hoverI, m_hoverJ, -1);

            // V (2x2) needs the +1 cell in bounds too; B and X just need the
            // hovered cell to exist, which `pick.valid` already guarantees.
            if (Input::IsKeyPressed(Key::B))
                PlaceBuilding(m_hoverCellX, m_hoverCellY, 1, 1, kColor1x1);
            if (Input::IsKeyPressed(Key::V)
                && m_hoverCellX <= kGridW - 2 && m_hoverCellY <= kGridH - 2)
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
        // drawn, any building whose FRONT-most cell lies on that diagonal is
        // drawn before moving on. Front-most cell = (origin.x + w - 1,
        // origin.y + h - 1). This keeps the building visually anchored to its
        // own footprint while still letting later diagonals (cells in front of
        // the building) draw over it correctly.
        const int maxSum = (kGridW - 1) + (kGridH - 1);
        for (int sum = 0; sum <= maxSum; ++sum) {
            const int xMin = Max(0, sum - (kGridH - 1));
            const int xMax = Min(kGridW - 1, sum);
            for (int x = xMin; x <= xMax; ++x)
                DrawCell(x, sum - x);

            for (const Building& b : m_buildings) {
                const int frontSum = (b.x + b.w - 1) + (b.y + b.h - 1);
                if (frontSum == sum)
                    DrawBuilding(b);
            }
        }

        // Hover marker on top of everything.
        if (m_hoverCornerValid) {
            const int hh = Corner(m_hoverI, m_hoverJ);
            const Vec2 p = CornerToScreen(m_hoverI, m_hoverJ, hh);
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
        c.title = "NaraGame - Citybuilder (buildings)";
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

    // CPU face-based picker: walks all cells, projects each cell's 4 corners
    // with elevation, tests point-in-quad against the cursor, then among hits
    // keeps the visually-frontmost — highest x+y, tiebreak by baseElev (a
    // higher cell at the same diagonal pokes further up the screen so it sits
    // visually in front). The corner is the closest of the picked cell's 4 in
    // screen space, also elevation-aware. O(kGridW * kGridH) per frame; trivial
    // at this scale.
    PickResult Pick(float mx, float my) const {
        PickResult r;
        int bestKey = -1;
        for (int y = 0; y < kGridH; ++y) {
            for (int x = 0; x < kGridW; ++x) {
                const int hT = Corner(x,     y    );
                const int hR = Corner(x + 1, y    );
                const int hB = Corner(x + 1, y + 1);
                const int hL = Corner(x,     y + 1);
                const Vec2 sT = CornerToScreen(x,     y,     hT);
                const Vec2 sR = CornerToScreen(x + 1, y,     hR);
                const Vec2 sB = CornerToScreen(x + 1, y + 1, hB);
                const Vec2 sL = CornerToScreen(x,     y + 1, hL);
                if (!PointInQuad({mx, my}, sT, sR, sB, sL))
                    continue;
                const int baseElev = Min(Min(hT, hR), Min(hB, hL));
                const int key = (x + y) * (kMaxHeight + 1) + baseElev;
                if (key <= bestKey)
                    continue;
                bestKey = key;
                r.valid = true;
                r.cellX = x;
                r.cellY = y;
                // Nearest of the 4 corners to the cursor, in screen space.
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

    int  Corner(int i, int j) const { return m_corners[j * kCornerCols + i]; }
    int& Corner(int i, int j)       { return m_corners[j * kCornerCols + i]; }

    void GetCellSlope(int x, int y, int& baseElev, int& slopeBits) const {
        const int hT = Corner(x,     y);
        const int hR = Corner(x + 1, y);
        const int hB = Corner(x + 1, y + 1);
        const int hL = Corner(x,     y + 1);
        baseElev = Min(Min(hT, hR), Min(hB, hL));
        slopeBits = ((hT > baseElev) ? 1 : 0)
                  | ((hR > baseElev) ? 2 : 0)
                  | ((hB > baseElev) ? 4 : 0)
                  | ((hL > baseElev) ? 8 : 0);
    }

    void DrawCell(int x, int y) {
        int baseElev = 0, slopeBits = 0;
        GetCellSlope(x, y, baseElev, slopeBits);

        const Vec2 center = TileCenterToScreen(x, y);
        const Vec2 topLeft{
            center.x - kSpriteW * 0.5f,
            center.y - kSpriteAnchorY - static_cast<float>(baseElev * kStepHeight)
        };

        const bool darkCheck = ((x + y) & 1) == 0;
        const float ev = static_cast<float>(baseElev);
        Color tint = darkCheck
            ? Color(0.40f + 0.06f * ev, 0.62f + 0.06f * ev, 0.30f + 0.06f * ev, 1.0f)
            : Color(0.50f + 0.06f * ev, 0.74f + 0.06f * ev, 0.38f + 0.06f * ev, 1.0f);
        // Lambert shading per slope direction makes the 3D shape read.
        const float b = SlopeBrightness(slopeBits);
        tint.r = Clamp(tint.r * b, 0.0f, 1.0f);
        tint.g = Clamp(tint.g * b, 0.0f, 1.0f);
        tint.b = Clamp(tint.b * b, 0.0f, 1.0f);

        m_renderer.DrawQuad(topLeft, {kSpriteW, kSpriteH}, m_slopes.At(slopeBits), tint);
    }

    // Lowest corner anywhere in the footprint — the building sits at this
    // elevation so it doesn't float above an uneven patch. For SC2K-strict
    // play we'd reject placement on uneven terrain entirely; for now we just
    // anchor to the lowest corner.
    int FootprintBaseElev(const Building& b) const {
        int e = INT_MAX;
        for (int j = b.y; j <= b.y + b.h; ++j)
            for (int i = b.x; i <= b.x + b.w; ++i)
                e = Min(e, Corner(i, j));
        return e == INT_MAX ? 0 : e;
    }

    void DrawBuilding(const Building& b) {
        // Project the footprint's tile-space center to screen, then drop it by
        // baseElev. Building extends UP from this anchor as a solid colored
        // box; height/width scale loosely with footprint so 1x1 vs 2x2 read at
        // different visual masses.
        const float mtx = b.x + (b.w - 1) * 0.5f;
        const float mty = b.y + (b.h - 1) * 0.5f;
        const float groundCx = (mtx - mty) * kHalfW + kOriginX;
        const float groundCy = (mtx + mty) * kHalfH + kOriginY;
        const int baseElev = FootprintBaseElev(b);
        const float screenBottomY = groundCy - static_cast<float>(baseElev * kStepHeight);

        // Per-size prototype dimensions.
        float boxW = 0.0f, boxH = 0.0f;
        if (b.w == 1 && b.h == 1) { boxW = 30.0f; boxH = 36.0f; }
        else if (b.w == 2 && b.h == 2) { boxW = 72.0f; boxH = 60.0f; }
        else { boxW = 24.0f * (b.w + b.h); boxH = 28.0f * Max(b.w, b.h); }

        // Sit the box on the ground center; bottom edge aligns with screenBottomY.
        m_renderer.DrawQuad({ groundCx - boxW * 0.5f, screenBottomY - boxH },
                            { boxW, boxH }, b.color);
    }

    void AdjustCorner(int i, int j, int delta) {
        const int before = Corner(i, j);
        const int after  = Clamp(before + delta, kMinHeight, kMaxHeight);
        if (after == before)
            return;
        Corner(i, j) = after;
        EnforceSlopeLimitFrom(i, j);
    }

    void EnforceSlopeLimitFrom(int startI, int startJ) {
        std::vector<std::pair<int, int>> queue;
        queue.emplace_back(startI, startJ);
        while (!queue.empty()) {
            const auto [ci, cj] = queue.back();
            queue.pop_back();
            const int h = Corner(ci, cj);
            for (int dj = -1; dj <= 1; ++dj) {
                for (int di = -1; di <= 1; ++di) {
                    if (di == 0 && dj == 0)
                        continue;
                    const int ni = ci + di;
                    const int nj = cj + dj;
                    if (ni < 0 || nj < 0 || ni >= kCornerCols || nj >= kCornerRows)
                        continue;
                    const int nh = Corner(ni, nj);
                    int target = nh;
                    if (nh < h - 1)      target = h - 1;
                    else if (nh > h + 1) target = h + 1;
                    target = Clamp(target, kMinHeight, kMaxHeight);
                    if (target != nh) {
                        Corner(ni, nj) = target;
                        queue.emplace_back(ni, nj);
                    }
                }
            }
        }
    }

    void PlaceBuilding(int x, int y, int w, int h, const Color& color) {
        // Reject overlap with existing buildings so a place key doesn't quietly
        // stack two sprites in the same footprint.
        for (const Building& b : m_buildings) {
            const bool overlapX = !(x + w <= b.x || b.x + b.w <= x);
            const bool overlapY = !(y + h <= b.y || b.y + b.h <= y);
            if (overlapX && overlapY)
                return;
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

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_slopesTexture;
    SpriteSheet m_slopes;
    std::vector<int> m_corners;
    std::vector<Building> m_buildings;

    int  m_hoverI = -1, m_hoverJ = -1;
    bool m_hoverCornerValid = false;
    int  m_hoverCellX = -1, m_hoverCellY = -1;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CitybuilderApp();
}
