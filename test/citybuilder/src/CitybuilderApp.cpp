// Citybuilder — milestone (b): corner-height terrain + 16-variant slope atlas.
//
// Builds on the milestone (a) flat iso grid by storing a corner-height array
// (one int per grid intersection — corners are shared between up to 4 cells),
// deriving each cell's slope variant from the 4-bit corner-elevation mask,
// and blitting the matching sprite out of a packed 4x4 atlas. A stepped
// pyramid is seeded so every slope shape — flat, single-corner, ridge,
// three-corner — appears on screen at once for visual verification.
//
// Hover picking still uses the flat-plane inverse projection from (a); it
// will mis-pick over elevated terrain. That is deferred to milestone (e).

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
#include <memory>
#include <string>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

// Grid + iso projection (unchanged from milestone (a)).
constexpr int   kGridW   = 16;
constexpr int   kGridH   = 16;
constexpr float kTileW   = 64.0f;
constexpr float kTileH   = 32.0f;
constexpr float kHalfW   = kTileW * 0.5f;
constexpr float kHalfH   = kTileH * 0.5f;
constexpr float kOriginX = 512.0f;
constexpr float kOriginY = 80.0f; // a bit lower than (a) to leave headroom for raised peaks

// Slope sprite geometry. The atlas cell is 64x40: the flat diamond sits in the
// bottom 32 rows (y = 8..39 within the cell) and the top 8-row strip is empty
// space that raised corners can poke into. kSpriteAnchorY is the y offset from
// sprite top-left to the flat-diamond CENTER, used to align world position to
// the sprite.
constexpr float kSpriteW       = 64.0f;
constexpr float kSpriteH       = 40.0f;
constexpr float kSpriteAnchorY = 23.0f; // (8 + 39) / 2 -> center of the flat diamond
constexpr int   kStepHeight    = 8;     // pixels per elevation level (matches sprite empty strip)

// Corner grid sizing. Corners live at integer tile-space coordinates, cells
// occupy the unit squares between them — so an NxN cell grid has (N+1)x(N+1)
// corners. A single corner is shared between up to 4 neighbouring cells.
constexpr int kCornerCols = kGridW + 1;
constexpr int kCornerRows = kGridH + 1;

// Mapping cell -> screen.
Vec2 TileCenterToScreen(int tx, int ty) {
    return { (tx - ty) * kHalfW + kOriginX, (tx + ty) * kHalfH + kOriginY };
}

// Inverse of the flat-plane projection (no elevation). Returns the cell whose
// rhombus contains the screen pixel; floors land inside the right diamond
// because the rhombus lattice tiles the plane without gaps.
void ScreenToTile(float sx, float sy, int& tx, int& ty) {
    const float dx = (sx - kOriginX) / kHalfW;
    const float dy = (sy - kOriginY) / kHalfH;
    tx = static_cast<int>(std::floor((dx + dy) * 0.5f));
    ty = static_cast<int>(std::floor((dy - dx) * 0.5f));
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

        // 4x4 atlas of 64x40 cells. Index = 4-bit corner mask: bit 0 = TOP raised,
        // bit 1 = RIGHT, bit 2 = BOTTOM, bit 3 = LEFT. Index 0 is the flat tile;
        // index 15 is "all four corners raised" — geometrically identical to flat
        // but at +1 elevation, so it's stored as a real flat diamond in the atlas.
        m_slopesTexture = LoadTexture(GetDevice(), assets + "/slopes.png");
        m_slopes = SpriteSheet(m_slopesTexture.get(), 64, 40);

        BuildDemoTerrain();
        NARAGAME_INFO("Citybuilder: stepped pyramid terrain. ESC to quit.");
    }

    void OnStop() override {
        m_slopesTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float /*dt*/) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        // Flat-plane picking — known to mis-pick over elevated terrain; that's
        // milestone (e). Useful here just to confirm the input pipeline.
        const Vec2 mp = Input::MousePosition();
        ScreenToTile(mp.x, mp.y, m_hoverX, m_hoverY);
    }

    void OnRender() override {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        GetDevice().Clear(Color(0.08f, 0.10f, 0.14f, 1.0f));
        m_renderer.Begin(Renderer2D::ScreenCamera(w, h));

        // Back-to-front by iso "depth" sum = x + y. Inside a diagonal, cells are
        // at the same screen-y so their bounding boxes don't fight; we walk
        // x ascending arbitrarily. With elevation, the higher cell along the
        // same diagonal pokes UP into the previous diagonal's box, but since
        // the previous diagonal already drew (its screen-y is smaller), this
        // ordering still produces correct occlusion for monotone hills.
        const int maxSum = (kGridW - 1) + (kGridH - 1);
        for (int sum = 0; sum <= maxSum; ++sum) {
            const int xMin = Max(0, sum - (kGridH - 1));
            const int xMax = Min(kGridW - 1, sum);
            for (int x = xMin; x <= xMax; ++x) {
                const int y = sum - x;
                DrawCell(x, y);
            }
        }

        m_renderer.End();
    }

private:
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title = "NaraGame - Citybuilder (slope variants)";
        c.width = 1024;
        c.height = 640;
        return c;
    }

    int  Corner(int i, int j) const { return m_corners[j * kCornerCols + i]; }
    int& Corner(int i, int j)       { return m_corners[j * kCornerCols + i]; }

    // Read the 4 corners of cell (x, y) — TOP, RIGHT, BOTTOM, LEFT in screen
    // terms — then derive base elevation (min corner) and the 4-bit "this
    // corner is above base" mask used to pick a slope sprite.
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
        // Anchor the sprite so its flat-diamond CENTER lines up with the cell's
        // ground center, then shift the whole sprite up by baseElev * step.
        const Vec2 topLeft{
            center.x - kSpriteW * 0.5f,
            center.y - kSpriteAnchorY - static_cast<float>(baseElev * kStepHeight)
        };

        // Subtle checker by base elevation so flats read; tint with elevation so
        // higher plateaus get lighter (cheap fake light) and the layering is
        // obvious without a real lighting pass.
        const bool darkCheck = ((x + y) & 1) == 0;
        const float ev = static_cast<float>(baseElev);
        Color tint = darkCheck
            ? Color(0.40f + 0.06f * ev, 0.62f + 0.06f * ev, 0.30f + 0.06f * ev, 1.0f)
            : Color(0.50f + 0.06f * ev, 0.74f + 0.06f * ev, 0.38f + 0.06f * ev, 1.0f);
        if (x == m_hoverX && y == m_hoverY)
            tint = Color(1.0f, 0.94f, 0.38f, 1.0f);

        m_renderer.DrawQuad(topLeft, {kSpriteW, kSpriteH}, m_slopes.At(slopeBits), tint);
    }

    // Seed a stepped pyramid centered on corner (8, 8). Three terraces of
    // ascending elevation give every interesting slope mask (single corner,
    // two-adjacent, three-of-four) along their perimeters. The apex corner is
    // surrounded by four cells each showing a different single-corner slope.
    void BuildDemoTerrain() {
        std::fill(m_corners.begin(), m_corners.end(), 0);
        // Outer terrace 5x5 corners at height 1
        for (int j = 6; j <= 10; ++j)
            for (int i = 6; i <= 10; ++i)
                Corner(i, j) = 1;
        // Middle terrace 3x3 at height 2
        for (int j = 7; j <= 9; ++j)
            for (int i = 7; i <= 9; ++i)
                Corner(i, j) = 2;
        // Apex (single corner) at height 3
        Corner(8, 8) = 3;
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_slopesTexture;
    SpriteSheet m_slopes;
    std::vector<int> m_corners; // (kCornerCols * kCornerRows), row-major
    int m_hoverX = -1;
    int m_hoverY = -1;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CitybuilderApp();
}
