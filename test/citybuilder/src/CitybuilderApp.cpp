// Citybuilder — milestone (c): interactive terraforming with slope-limit propagation.
//
// Adds the SC2K "magic" — left-click raises the corner nearest the cursor by one
// step, right-click lowers it. Because corners are shared between up to four
// cells, a single edit visibly reshapes all four touching cells at once. To
// keep the heightfield representable by the (one-step-max) slope atlas, every
// edit triggers an 8-connected BFS that drags neighbouring corners along so no
// pair of corners differs by more than one step. The result is exactly the
// "raising one corner of a hill cascades outward" behaviour from SC2K.
//
// The terrain starts flat (the seeded pyramid from milestone (b) is dropped —
// terraforming is now the only source of relief, so the UX is the thing being
// tested). Hover and click picking are flat-plane projections; over an
// elevated hill they'll pick the corner geometrically under the cursor, not
// the one drawn at the cursor position. True elevation-aware picking is
// milestone (e).

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
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

// Grid + iso projection (unchanged from milestones (a)/(b)).
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

constexpr int kMaxHeight = 8;  // ~64 px tall hills at the cap; plenty for prototyping
constexpr int kMinHeight = 0;

// Cell (x, y) ground-center on the flat plane.
Vec2 TileCenterToScreen(int tx, int ty) {
    return { (tx - ty) * kHalfW + kOriginX, (tx + ty) * kHalfH + kOriginY };
}

// Inverse of the flat-plane CELL projection — unchanged from (a)/(b).
void ScreenToTile(float sx, float sy, int& tx, int& ty) {
    const float dx = (sx - kOriginX) / kHalfW;
    const float dy = (sy - kOriginY) / kHalfH;
    tx = static_cast<int>(std::floor((dx + dy) * 0.5f));
    ty = static_cast<int>(std::floor((dy - dx) * 0.5f));
}

// Corner (i, j) sits at tile-space (i - 0.5, j - 0.5), so its flat-plane
// screen projection is ((i - j) * halfW + origin, (i + j - 1) * halfH + origin).
// The `height` arg shifts it up by that many step-pixels for the visual marker.
Vec2 CornerToScreen(int i, int j, int height) {
    return {
        (i - j) * kHalfW + kOriginX,
        (i + j - 1) * kHalfH + kOriginY - static_cast<float>(height * kStepHeight)
    };
}

// Inverse of CornerToScreen on the flat plane. Returns the nearest integer
// corner (rounded), with an out-of-grid sentinel of -1 when the cursor sits
// well outside the corner lattice's bounding rhombus.
bool ScreenToCorner(float sx, float sy, int& outI, int& outJ) {
    // Solve the corner forward map for real-valued (i, j):
    //   dx / halfW = i - j           and    (dy / halfH) + 1 = i + j
    // → i = (dx/halfW + dy/halfH + 1) / 2
    //   j = (dy/halfH - dx/halfW + 1) / 2
    const float dx = sx - kOriginX;
    const float dy = sy - kOriginY;
    const float fi = (dx / kHalfW + dy / kHalfH + 1.0f) * 0.5f;
    const float fj = (dy / kHalfH - dx / kHalfW + 1.0f) * 0.5f;
    if (fi < -0.5f || fi > kGridW + 0.5f || fj < -0.5f || fj > kGridH + 0.5f) {
        outI = outJ = -1;
        return false;
    }
    outI = static_cast<int>(std::lround(fi));
    outJ = static_cast<int>(std::lround(fj));
    if (outI < 0) outI = 0;  else if (outI > kGridW) outI = kGridW;
    if (outJ < 0) outJ = 0;  else if (outJ > kGridH) outJ = kGridH;
    return true;
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
        NARAGAME_INFO("Citybuilder: left-click raises a corner, right-click lowers. ESC to quit.");
    }

    void OnStop() override {
        m_slopesTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float /*dt*/) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        const Vec2 mp = Input::MousePosition();
        m_hoverValid = ScreenToCorner(mp.x, mp.y, m_hoverI, m_hoverJ);

        // Edge-detect clicks so a held button doesn't terraform every frame.
        if (m_hoverValid) {
            if (Input::IsMousePressed(MouseButton::Left))  AdjustCorner(m_hoverI, m_hoverJ, +1);
            if (Input::IsMousePressed(MouseButton::Right)) AdjustCorner(m_hoverI, m_hoverJ, -1);
        }
    }

    void OnRender() override {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        GetDevice().Clear(Color(0.08f, 0.10f, 0.14f, 1.0f));
        m_renderer.Begin(Renderer2D::ScreenCamera(w, h));

        // Back-to-front by diagonal x+y (correct for monotone hills).
        const int maxSum = (kGridW - 1) + (kGridH - 1);
        for (int sum = 0; sum <= maxSum; ++sum) {
            const int xMin = Max(0, sum - (kGridH - 1));
            const int xMax = Min(kGridW - 1, sum);
            for (int x = xMin; x <= xMax; ++x)
                DrawCell(x, sum - x);
        }

        // Corner marker drawn last so it sits on top of every tile. A small
        // bright square at the corner's elevated screen position tells the
        // user exactly what they're about to edit.
        if (m_hoverValid) {
            const int h_at = Corner(m_hoverI, m_hoverJ);
            const Vec2 p = CornerToScreen(m_hoverI, m_hoverJ, h_at);
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
        c.title = "NaraGame - Citybuilder (terraform)";
        c.width = 1024;
        c.height = 640;
        return c;
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
        const Color tint = darkCheck
            ? Color(0.40f + 0.06f * ev, 0.62f + 0.06f * ev, 0.30f + 0.06f * ev, 1.0f)
            : Color(0.50f + 0.06f * ev, 0.74f + 0.06f * ev, 0.38f + 0.06f * ev, 1.0f);

        m_renderer.DrawQuad(topLeft, {kSpriteW, kSpriteH}, m_slopes.At(slopeBits), tint);
    }

    // Single entry point for raise (+1) / lower (-1) so the propagation only
    // exists in one place. After the corner moves, BFS drags any neighbour that
    // would have ended up more than one step away — the SC2K cascade.
    void AdjustCorner(int i, int j, int delta) {
        const int before = Corner(i, j);
        const int after  = Clamp(before + delta, kMinHeight, kMaxHeight);
        if (after == before)
            return;
        Corner(i, j) = after;
        EnforceSlopeLimitFrom(i, j);
    }

    // 8-connected BFS: any neighbour outside [self-1, self+1] is dragged into
    // range; if that touched it, its own neighbours are re-checked. Symmetric
    // in raise/lower direction — a raise pulls low neighbours up, a lower
    // pushes high neighbours down. Clamped to [kMinHeight, kMaxHeight] so the
    // cascade can't run off the height limits; the clamp is consistent with
    // AdjustCorner so propagation never reintroduces a height the player
    // couldn't have set themselves.
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

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_slopesTexture;
    SpriteSheet m_slopes;
    std::vector<int> m_corners; // (kCornerCols * kCornerRows), row-major

    int  m_hoverI = -1;
    int  m_hoverJ = -1;
    bool m_hoverValid = false;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CitybuilderApp();
}
