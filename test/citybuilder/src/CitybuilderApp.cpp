// Citybuilder — milestone 1: flat diamond iso grid + mouse hover highlight.
//
// This test app exists to prototype SimCity-2000-style quarter-view rendering
// outside the engine. Once corner-height terrain, painter-order draw, and
// elevation-aware picking shake out here, the reusable pieces will be promoted
// into an engine `IsoTerrain` class. For now: a flat NxN diamond grid is drawn
// in row-major order (which already equals back-to-front painter order when
// elevation is zero — both +x and +y move the tile down the screen), and the
// mouse cursor is inverse-projected to a (tileX, tileY) cell that gets a
// yellow highlight tint.

#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/TextureRegion.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <cmath>
#include <memory>
#include <string>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

// Grid dimensions (cells) and tile dimensions (the diamond's bounding-box pixels).
// 2:1 width:height is the classic SC2000 quarter-view aspect.
constexpr int   kGridW   = 16;
constexpr int   kGridH   = 16;
constexpr float kTileW   = 64.0f;
constexpr float kTileH   = 32.0f;
constexpr float kHalfW   = kTileW * 0.5f;
constexpr float kHalfH   = kTileH * 0.5f;

// Screen-space anchor for cell (0,0)'s diamond CENTER. The grid extends:
//   right edge: cell (W-1, 0) -> +x = (W-1) * kHalfW
//   left  edge: cell (0, H-1) -> -x = (H-1) * kHalfW
//   bottom    : cell (W-1, H-1) -> +y = (W+H-2) * kHalfH
// So a 16x16 grid spans 16*kTileW wide x 16*kTileH tall = 1024 x 512.
constexpr float kOriginX = 512.0f; // centered horizontally in a 1024-wide window
constexpr float kOriginY = 48.0f;  // small top margin

// Forward (tile -> screen): the screen position of cell (tx, ty)'s diamond center.
// Standard 2:1 iso projection: x stride is half the tile width per cell-x and
// minus half per cell-y; y stride is half the tile height per cell-x AND per
// cell-y. Cell (1,0) sits diagonally down-and-right, (0,1) diagonally down-and-left.
Vec2 TileCenterToScreen(int tx, int ty) {
    return { (tx - ty) * kHalfW + kOriginX, (tx + ty) * kHalfH + kOriginY };
}

// Inverse (screen -> tile). Works because the rhombus lattice tiles the plane
// without gaps: any screen point lies in exactly one diamond, and the integer
// cell coords are the floors of the algebraic solution to the forward map.
// Returns (-1, -1)-ish coords for clicks outside the grid; the caller bounds-checks.
void ScreenToTile(float sx, float sy, int& tx, int& ty) {
    const float dx = (sx - kOriginX) / kHalfW; // = (tx - ty), continuous
    const float dy = (sy - kOriginY) / kHalfH; // = (tx + ty), continuous
    tx = static_cast<int>(std::floor((dx + dy) * 0.5f));
    ty = static_cast<int>(std::floor((dy - dx) * 0.5f));
}

} // namespace

class CitybuilderApp : public Application {
public:
    CitybuilderApp() : Application(MakeConfig()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;

        // The tile PNG is a white diamond with a slightly darker rim. Drawn with
        // a color tint, the rim stays proportionally darker so cells read as 3D.
        m_tileTexture = LoadTexture(GetDevice(), assets + "/iso_tile.png");
        m_tileRegion = TextureRegion::FromPixels(m_tileTexture.get(), 0.0f, 0.0f, kTileW, kTileH);

        NARAGAME_INFO("Citybuilder: move the mouse to highlight a cell. ESC to quit.");
    }

    void OnStop() override {
        m_tileTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float /*dt*/) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        const Vec2 mp = Input::MousePosition();
        ScreenToTile(mp.x, mp.y, m_hoverX, m_hoverY);
    }

    void OnRender() override {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        GetDevice().Clear(Color(0.08f, 0.10f, 0.14f, 1.0f));
        m_renderer.Begin(Renderer2D::ScreenCamera(w, h));

        // Back-to-front draw. With elevation=0, row-major (ty outer, tx inner)
        // happens to coincide with painter's order because (tx + ty) increases
        // monotonically as you walk this loop diagonally. The structure is kept
        // explicit so the eventual elevated version can swap in a proper depth
        // sort without reshuffling everything else.
        for (int ty = 0; ty < kGridH; ++ty) {
            for (int tx = 0; tx < kGridW; ++tx) {
                const Vec2 center = TileCenterToScreen(tx, ty);
                const Vec2 topLeft{ center.x - kHalfW, center.y - kHalfH };

                // Subtle checkerboard so the flat grid is readable as a grid.
                const bool dark = ((tx + ty) & 1) == 0;
                Color tint = dark ? Color(0.42f, 0.66f, 0.32f, 1.0f)
                                  : Color(0.52f, 0.78f, 0.40f, 1.0f);
                if (tx == m_hoverX && ty == m_hoverY)
                    tint = Color(1.0f, 0.94f, 0.38f, 1.0f);

                m_renderer.DrawQuad(topLeft, {kTileW, kTileH}, m_tileRegion, tint);
            }
        }

        m_renderer.End();
    }

private:
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title = "NaraGame - Citybuilder (iso prototype)";
        c.width = 1024;
        c.height = 640;
        return c;
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_tileTexture;
    TextureRegion m_tileRegion;
    int m_hoverX = -1;
    int m_hoverY = -1;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CitybuilderApp();
}
