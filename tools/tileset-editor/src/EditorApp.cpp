// NaraGame tileset / tilemap editor.
//
// Layout (1280 x 800):
//   Toolbar (top, 36 px)  : current file path, tileset path, map size, mouse cell
//   Palette (left, 256 px): scrollable tileset; click to pick a tile id
//   Canvas  (right, fill) : the tilemap; left-paint, right-erase, middle-pan,
//                           wheel-zoom; arrow keys pan
//
// Keyboard
//   Esc          quit
//   G            toggle grid overlay
//   Ctrl+N       new map (default size, current tileset)
//   Ctrl+S       save to current path (.ntmap)
//   Ctrl+O       reload current path (.ntmap or .tmx)
//   + / -        zoom in / out (wheel works too)
//
// Command line
//   tileset-editor [map]
//       map: optional .ntmap or .tmx to open
//   tileset-editor --tileset PATH --tilesize N [--mapsize W H] [--out PATH]
//       start a fresh map with the given tileset and dimensions; --out is
//       where Ctrl+S writes (default: a sibling .ntmap next to the tileset).

#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/assets/TilemapLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/SpriteSheet.h>
#include <NaraGame/renderer/Tilemap.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace NaraGame;

namespace {

constexpr float kScreenW   = 1280.0f;
constexpr float kScreenH   = 800.0f;
constexpr float kToolbarH  = 36.0f;
constexpr float kPaletteW  = 256.0f;
constexpr float kStatusH   = 24.0f;

constexpr int   kDefaultMapW    = 32;
constexpr int   kDefaultMapH    = 24;
constexpr int   kDefaultTileSz  = 32;

const Color kBgClear     = Color(0.10f, 0.11f, 0.13f, 1.0f);
const Color kPaneBg      = Color(0.15f, 0.16f, 0.19f, 1.0f);
const Color kCanvasBg    = Color(0.18f, 0.20f, 0.23f, 1.0f);
const Color kCanvasOut   = Color(0.08f, 0.09f, 0.10f, 1.0f);
const Color kToolbarBg   = Color(0.20f, 0.22f, 0.27f, 1.0f);
const Color kGridLine    = Color(0.0f, 0.0f, 0.0f, 0.18f);
const Color kSelectHi    = Color(1.00f, 0.86f, 0.20f, 1.0f);
const Color kHoverHi     = Color(1.00f, 1.00f, 1.00f, 0.35f);
const Color kEmptyTint   = Color(0.35f, 0.38f, 0.45f, 0.40f);
const Color kStatusText  = Color(0.86f, 0.88f, 0.94f, 1.0f);

bool EndsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const char a = char(std::tolower((unsigned char)s[s.size() - n + i]));
        const char b = char(std::tolower((unsigned char)suffix[i]));
        if (a != b) return false;
    }
    return true;
}

std::string ReplaceExt(std::string path, const char* newExt) {
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) path.resize(dot);
    path += newExt;
    return path;
}

bool IsInside(Vec2 p, Vec2 origin, Vec2 size) {
    return p.x >= origin.x && p.y >= origin.y &&
           p.x <  origin.x + size.x && p.y <  origin.y + size.y;
}

void DrawRectOutline(Renderer2D& r, Vec2 pos, Vec2 size, float thick,
                     const Color& c) {
    r.DrawQuad(pos, {size.x, thick}, c);
    r.DrawQuad({pos.x, pos.y + size.y - thick}, {size.x, thick}, c);
    r.DrawQuad(pos, {thick, size.y}, c);
    r.DrawQuad({pos.x + size.x - thick, pos.y}, {thick, size.y}, c);
}

// ---------------------------------------------------------------------------
// Bitmap digit/letter renderer (procedural). We don't pull in a font asset:
// the editor only needs to render small status text (file path, tile id,
// dimensions, hover cell). A 3x5 mono font built from solid quads is enough.
// ---------------------------------------------------------------------------
struct MicroFont {
    static int GlyphIndex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
        if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
        switch (c) {
            case '.': return 36;
            case ',': return 37;
            case ':': return 38;
            case '/': return 39;
            case '\\': return 40;
            case '-': return 41;
            case '_': return 42;
            case ' ': return 43;
            case '#': return 44;
            case '+': return 45;
            case '(': return 46;
            case ')': return 47;
            case 'x': return 10 + ('X' - 'A');
            default: return 44; // hash for unknown
        }
    }
    // Each glyph: 5 rows, 3 columns; 1 = solid, packed row-major. Indices
    // match GlyphIndex().
    static const char* Bits(int idx) {
        static const char* k[] = {
            "111""101""101""101""111", // 0
            "010""110""010""010""111", // 1
            "111""001""111""100""111", // 2
            "111""001""111""001""111", // 3
            "101""101""111""001""001", // 4
            "111""100""111""001""111", // 5
            "111""100""111""101""111", // 6
            "111""001""010""100""100", // 7
            "111""101""111""101""111", // 8
            "111""101""111""001""111", // 9
            "111""101""111""101""101", // A
            "110""101""110""101""110", // B
            "111""100""100""100""111", // C
            "110""101""101""101""110", // D
            "111""100""110""100""111", // E
            "111""100""110""100""100", // F
            "111""100""101""101""111", // G
            "101""101""111""101""101", // H
            "111""010""010""010""111", // I
            "001""001""001""101""111", // J
            "101""101""110""101""101", // K
            "100""100""100""100""111", // L
            "101""111""111""101""101", // M
            "101""111""111""111""101", // N
            "111""101""101""101""111", // O
            "111""101""111""100""100", // P
            "111""101""101""111""001", // Q
            "111""101""110""101""101", // R
            "111""100""111""001""111", // S
            "111""010""010""010""010", // T
            "101""101""101""101""111", // U
            "101""101""101""101""010", // V
            "101""101""111""111""101", // W
            "101""101""010""101""101", // X
            "101""101""010""010""010", // Y
            "111""001""010""100""111", // Z
            "000""000""000""000""010", // .
            "000""000""000""010""100", // ,
            "000""010""000""010""000", // :
            "001""001""010""100""100", // /
            "100""100""010""001""001", // backslash
            "000""000""111""000""000", // -
            "000""000""000""000""111", // _
            "000""000""000""000""000", // space
            "010""111""010""111""010", // #
            "000""010""111""010""000", // +
            "010""100""100""100""010", // (
            "010""001""001""001""010", // )
        };
        return k[idx];
    }
};

void DrawMicroText(Renderer2D& r, const std::string& text, Vec2 pos,
                   float pixelSize, const Color& color) {
    const float px = std::max(1.0f, std::floor(pixelSize / 5.0f));
    const float cellW = 3.0f * px;
    const float cellH = 5.0f * px;
    const float advance = cellW + px;        // 1px spacing between glyphs
    float x = pos.x;
    const float y = pos.y;
    for (char c : text) {
        if (c == '\n') { /* single-line for now */ continue; }
        const int idx = MicroFont::GlyphIndex(c);
        const char* bits = MicroFont::Bits(idx);
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (bits[row * 3 + col] == '1') {
                    r.DrawQuad({x + col * px, y + row * px}, {px, px}, color);
                }
            }
        }
        x += advance;
        (void)cellH;
    }
}

float MeasureMicroText(const std::string& text, float pixelSize) {
    const float px = std::max(1.0f, std::floor(pixelSize / 5.0f));
    const float advance = 3.0f * px + px;
    return advance * float(text.size()) - px;
}

// ---------------------------------------------------------------------------
struct EditorArgs {
    std::string mapPath;          // .ntmap or .tmx to open at start
    std::string tilesetPath;      // for --tileset (new-map mode)
    std::string outPath;          // explicit save path for new-map mode
    int tileSize = kDefaultTileSz;
    int mapW = kDefaultMapW;
    int mapH = kDefaultMapH;
};

EditorArgs ParseArgs(int argc, char** argv) {
    EditorArgs a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                NARAGAME_ERROR("editor: %s expects a value", what);
                return nullptr;
            }
            return argv[++i];
        };
        if (s == "--tileset") { if (auto v = next("--tileset")) a.tilesetPath = v; }
        else if (s == "--tilesize") {
            if (auto v = next("--tilesize")) a.tileSize = std::max(1, std::atoi(v));
        }
        else if (s == "--mapsize") {
            const char* w = next("--mapsize");
            const char* h = next("--mapsize");
            if (w && h) { a.mapW = std::max(1, std::atoi(w));
                          a.mapH = std::max(1, std::atoi(h)); }
        }
        else if (s == "--out") { if (auto v = next("--out")) a.outPath = v; }
        else if (!s.empty() && s[0] != '-') a.mapPath = s;
    }
    return a;
}

} // namespace

// ---------------------------------------------------------------------------

class EditorApp : public Application {
public:
    explicit EditorApp(EditorArgs args)
        : Application(MakeConfig()), m_args(std::move(args)) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());

        if (!m_args.mapPath.empty()) {
            OpenPath(m_args.mapPath);
        } else if (!m_args.tilesetPath.empty()) {
            StartNewMapWithTileset(m_args.tilesetPath,
                                   m_args.tileSize, m_args.tileSize,
                                   m_args.mapW, m_args.mapH);
            m_filePath = m_args.outPath.empty()
                ? ReplaceExt(m_args.tilesetPath, ".ntmap")
                : m_args.outPath;
        } else {
            NARAGAME_INFO("editor: no map or --tileset supplied. "
                          "Use Ctrl+N after launching with --tileset, "
                          "or pass a .ntmap / .tmx path.");
        }

        m_grid     = true;
        m_camera   = {0.0f, 0.0f};
        m_zoom     = 1.0f;
        m_paletteScroll = 0.0f;
    }

    void OnUpdate(float dt) override {
        const Vec2 mouse = Input::MousePosition();
        const float wheel = Input::MouseWheelDelta();
        const bool ctrl = Input::IsKeyDown(Key::LeftCtrl);

        // Quit.
        if (Input::IsKeyPressed(Key::Escape)) Close();

        // Toolbar/palette/canvas rectangles.
        const Vec2 paletteOrigin{0.0f, kToolbarH};
        const Vec2 paletteSize  {kPaletteW, kScreenH - kToolbarH - kStatusH};
        const Vec2 canvasOrigin {kPaletteW, kToolbarH};
        const Vec2 canvasSize   {kScreenW - kPaletteW,
                                 kScreenH - kToolbarH - kStatusH};

        m_inCanvas  = IsInside(mouse, canvasOrigin,  canvasSize);
        m_inPalette = IsInside(mouse, paletteOrigin, paletteSize);

        // ----- Global shortcuts -----
        if (Input::IsKeyPressed(Key::G)) m_grid = !m_grid;
        if (ctrl && Input::IsKeyPressed(Key::S) && m_map.Width() > 0) {
            SaveCurrent();
        }
        if (ctrl && Input::IsKeyPressed(Key::O) && !m_args.mapPath.empty()) {
            OpenPath(m_args.mapPath);
        }
        if (ctrl && Input::IsKeyPressed(Key::N)) {
            // Re-use current tileset if any; otherwise the CLI tileset.
            const std::string tsPath = m_currentTilesetPath.empty()
                ? m_args.tilesetPath : m_currentTilesetPath;
            if (!tsPath.empty()) {
                StartNewMapWithTileset(tsPath, m_tileW, m_tileH,
                                       kDefaultMapW, kDefaultMapH);
                m_filePath = ReplaceExt(tsPath, ".ntmap");
            }
        }

        // ----- Camera pan -----
        const float panSpeed = 400.0f / std::max(0.5f, m_zoom);
        if (Input::IsKeyDown(Key::Left))  m_camera.x -= panSpeed * dt;
        if (Input::IsKeyDown(Key::Right)) m_camera.x += panSpeed * dt;
        if (Input::IsKeyDown(Key::Up))    m_camera.y -= panSpeed * dt;
        if (Input::IsKeyDown(Key::Down))  m_camera.y += panSpeed * dt;

        // Middle-drag pan.
        if (Input::IsMousePressed(MouseButton::Middle) && m_inCanvas) {
            m_panning   = true;
            m_panAnchor = mouse;
            m_camAnchor = m_camera;
        }
        if (m_panning) {
            if (!Input::IsMouseDown(MouseButton::Middle)) {
                m_panning = false;
            } else {
                m_camera.x = m_camAnchor.x - (mouse.x - m_panAnchor.x) / m_zoom;
                m_camera.y = m_camAnchor.y - (mouse.y - m_panAnchor.y) / m_zoom;
            }
        }

        // ----- Zoom -----
        if (m_inCanvas && wheel != 0.0f) {
            const float prev = m_zoom;
            m_zoom = std::clamp(m_zoom * std::pow(1.15f, wheel),
                                0.25f, 8.0f);
            // Zoom around the cursor: keep the world point under the mouse
            // stationary across the change.
            const Vec2 worldBefore = ScreenToWorld(mouse, canvasOrigin, prev);
            const Vec2 worldAfter  = ScreenToWorld(mouse, canvasOrigin, m_zoom);
            m_camera.x += worldBefore.x - worldAfter.x;
            m_camera.y += worldBefore.y - worldAfter.y;
        }
        // ----- Palette interaction -----
        if (m_inPalette && wheel != 0.0f) {
            m_paletteScroll = std::max(0.0f,
                m_paletteScroll - wheel * float(m_tileH) * 2.0f);
        }
        if (m_inPalette && Input::IsMousePressed(MouseButton::Left) &&
            m_currentTilesetPath.size()) {
            const int picked = PaletteHit(mouse, paletteOrigin, paletteSize);
            if (picked >= 0) m_selectedTile = picked;
        }

        // ----- Canvas paint / erase -----
        if (m_inCanvas && m_map.Width() > 0 && !m_panning) {
            const Vec2 world = ScreenToWorld(mouse, canvasOrigin, m_zoom);
            const int tx = int(std::floor(world.x / float(m_tileW)));
            const int ty = int(std::floor(world.y / float(m_tileH)));
            m_hoverTx = tx;
            m_hoverTy = ty;
            const bool inMap = tx >= 0 && ty >= 0 &&
                               tx < m_map.Width() && ty < m_map.Height();
            if (inMap) {
                if (Input::IsMouseDown(MouseButton::Left)) {
                    m_map.SetTile(tx, ty, m_selectedTile);
                } else if (Input::IsMouseDown(MouseButton::Right)) {
                    m_map.SetTile(tx, ty, -1);
                }
            }
        } else {
            m_hoverTx = -1;
            m_hoverTy = -1;
        }
    }

    void OnRender() override {
        Renderer2D& r = m_renderer;
        r.Begin(Renderer2D::ScreenCamera(kScreenW, kScreenH));
        r.DrawQuad({0, 0}, {kScreenW, kScreenH}, kBgClear);

        DrawCanvasPanel(r);
        DrawPalettePanel(r);
        DrawToolbar(r);
        DrawStatusBar(r);

        r.End();
    }

    void OnStop() override {
        m_tilesetTex.reset();
        m_renderer.Shutdown();
    }

private:
    // ----- Helpers -----
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title  = "NaraGame Tileset Editor";
        c.width  = uint32_t(kScreenW);
        c.height = uint32_t(kScreenH);
        c.vsync  = true;
        return c;
    }

    Vec2 ScreenToWorld(Vec2 s, Vec2 canvasOrigin, float zoom) const {
        return { (s.x - canvasOrigin.x) / zoom + m_camera.x,
                 (s.y - canvasOrigin.y) / zoom + m_camera.y };
    }
    Vec2 WorldToScreen(Vec2 w, Vec2 canvasOrigin) const {
        return { (w.x - m_camera.x) * m_zoom + canvasOrigin.x,
                 (w.y - m_camera.y) * m_zoom + canvasOrigin.y };
    }

    void OpenPath(const std::string& path) {
        TilemapAsset asset;
        if (EndsWith(path, ".tmx")) {
            asset = LoadTmx(GetDevice(), path, TextureFilter::Point);
        } else {
            asset = LoadNtmap(GetDevice(), path, TextureFilter::Point);
        }
        if (!asset.valid) {
            NARAGAME_ERROR("editor: failed to open '%s'", path.c_str());
            return;
        }
        m_tilesetTex = std::move(asset.texture);
        m_map        = std::move(asset.map);
        // The Tilemap stored a SpriteSheet pointing at the asset's texture
        // before we moved it. Rebind so it points at our owned copy.
        m_tileW = asset.tileWidth;
        m_tileH = asset.tileHeight;
        SpriteSheet sheet(m_tilesetTex.get(), m_tileW, m_tileH, 0, 0);
        m_map.SetTileset(sheet);
        m_sheet = sheet;
        m_currentTilesetPath = asset.tilesetPath;
        m_filePath  = path;
        m_camera    = {0.0f, 0.0f};
        m_zoom      = 1.0f;
        m_paletteScroll = 0.0f;
        m_selectedTile  = 0;
        NARAGAME_INFO("editor: opened '%s' (%dx%d, tile %dx%d)",
                      path.c_str(), m_map.Width(), m_map.Height(),
                      m_tileW, m_tileH);
    }

    void StartNewMapWithTileset(const std::string& tilesetPath,
                                int tileW, int tileH, int mapW, int mapH) {
        m_tilesetTex = LoadTexture(GetDevice(), tilesetPath,
                                   TextureFilter::Point);
        if (!m_tilesetTex) {
            NARAGAME_ERROR("editor: failed to load tileset '%s'",
                           tilesetPath.c_str());
            return;
        }
        m_tileW = tileW;
        m_tileH = tileH;
        m_sheet = SpriteSheet(m_tilesetTex.get(), tileW, tileH, 0, 0);
        m_map = Tilemap(mapW, mapH, float(tileW), float(tileH));
        m_map.SetTileset(m_sheet);
        m_map.Fill(-1);
        m_currentTilesetPath = tilesetPath;
        m_camera = {0.0f, 0.0f};
        m_zoom   = 1.0f;
        m_paletteScroll = 0.0f;
        m_selectedTile  = 0;
        NARAGAME_INFO("editor: new map %dx%d, tile %dx%d, tileset '%s'",
                      mapW, mapH, tileW, tileH, tilesetPath.c_str());
    }

    void SaveCurrent() {
        if (m_filePath.empty() || m_currentTilesetPath.empty() ||
            m_map.Width() <= 0) {
            NARAGAME_WARN("editor: nothing to save (no map or no path)");
            return;
        }
        // If the user opened a .tmx, write the save as .ntmap next to it --
        // the editor's native format is .ntmap; we don't write Tiled XML.
        std::string out = m_filePath;
        if (EndsWith(out, ".tmx")) out = ReplaceExt(out, ".ntmap");
        if (!EndsWith(out, ".ntmap")) out += ".ntmap";
        const bool ok = SaveNtmap(out, m_map, m_currentTilesetPath,
                                  m_tileW, m_tileH, 0, 0);
        if (ok) {
            m_filePath = out;
            NARAGAME_INFO("editor: saved '%s'", out.c_str());
        }
    }

    // Layout the tileset palette as a grid of cells. Returns the picked tile
    // index, or -1 if the mouse missed every cell.
    void PaletteLayout(Vec2 paletteOrigin, Vec2 paletteSize,
                       float& outCellPx, int& outCols, int& outRows) const {
        outCellPx = 0.0f;
        outCols = 0;
        outRows = 0;
        if (!m_tilesetTex || m_sheet.Count() <= 0) return;
        // Palette cell pixel size: pick the largest that fits at least 4
        // columns into the panel, but never below 16 or above 64.
        const float pad = 8.0f;
        const float maxCellPx = std::min(64.0f,
            std::floor((paletteSize.x - pad * 2.0f - 16.0f) / 4.0f));
        const float minCellPx = std::min(maxCellPx, 16.0f);
        outCellPx = std::max(minCellPx, maxCellPx);
        outCols   = std::max(1, int(std::floor(
            (paletteSize.x - pad * 2.0f) / (outCellPx + 4.0f))));
        outRows   = (m_sheet.Count() + outCols - 1) / outCols;
        (void)paletteOrigin;
    }

    int PaletteHit(Vec2 mouse, Vec2 paletteOrigin, Vec2 paletteSize) const {
        float cellPx; int cols, rows;
        PaletteLayout(paletteOrigin, paletteSize, cellPx, cols, rows);
        if (cols <= 0) return -1;
        const float pad = 8.0f;
        const float pitch = cellPx + 4.0f;
        const float lx = mouse.x - paletteOrigin.x - pad;
        const float ly = mouse.y - paletteOrigin.y - pad + m_paletteScroll;
        if (lx < 0.0f || ly < 0.0f) return -1;
        const int cx = int(std::floor(lx / pitch));
        const int cy = int(std::floor(ly / pitch));
        const float gx = lx - cx * pitch;
        const float gy = ly - cy * pitch;
        if (cx < 0 || cy < 0 || cx >= cols) return -1;
        if (gx > cellPx || gy > cellPx) return -1;
        const int idx = cy * cols + cx;
        if (idx >= m_sheet.Count()) return -1;
        return idx;
    }

    // ----- Drawing -----
    void DrawCanvasPanel(Renderer2D& r) {
        const Vec2 origin{kPaletteW, kToolbarH};
        const Vec2 size  {kScreenW - kPaletteW,
                          kScreenH - kToolbarH - kStatusH};
        r.DrawQuad(origin, size, kCanvasOut);

        if (m_map.Width() <= 0) {
            // Empty state — just a hint.
            DrawMicroText(r,
                "no map loaded. pass a .ntmap or --tileset PATH to start.",
                {origin.x + 16.0f, origin.y + 16.0f}, 14.0f, kStatusText);
            return;
        }

        // Canvas background covering the map's world rect.
        const Vec2 wTL = WorldToScreen({0.0f, 0.0f}, origin);
        const float worldW = float(m_map.Width()  * m_tileW) * m_zoom;
        const float worldH = float(m_map.Height() * m_tileH) * m_zoom;
        r.DrawQuad(wTL, {worldW, worldH}, kCanvasBg);

        // Draw tiles. We submit them as plain DrawQuad calls so we control
        // the per-tile scale (Tilemap::Draw bakes its own world transform
        // and can't do per-tile zoom here).
        for (int ty = 0; ty < m_map.Height(); ++ty) {
            for (int tx = 0; tx < m_map.Width(); ++tx) {
                const int id = m_map.GetTile(tx, ty);
                if (id < 0) continue;
                const TextureRegion region = m_sheet.At(id);
                if (!region.Valid()) continue;
                const Vec2 p = WorldToScreen(
                    {float(tx * m_tileW), float(ty * m_tileH)}, origin);
                r.DrawQuad(p, {float(m_tileW) * m_zoom,
                                float(m_tileH) * m_zoom}, region);
            }
        }

        // Grid overlay.
        if (m_grid) {
            const float lineT = 1.0f;
            const Color gc = kGridLine;
            // Vertical lines.
            for (int x = 0; x <= m_map.Width(); ++x) {
                const Vec2 a = WorldToScreen({float(x * m_tileW), 0.0f}, origin);
                r.DrawQuad({a.x, a.y}, {lineT, worldH}, gc);
            }
            for (int y = 0; y <= m_map.Height(); ++y) {
                const Vec2 a = WorldToScreen({0.0f, float(y * m_tileH)}, origin);
                r.DrawQuad({a.x, a.y}, {worldW, lineT}, gc);
            }
        }

        // Hover highlight.
        if (m_hoverTx >= 0 && m_hoverTy >= 0 &&
            m_hoverTx < m_map.Width() && m_hoverTy < m_map.Height()) {
            const Vec2 hp = WorldToScreen(
                {float(m_hoverTx * m_tileW), float(m_hoverTy * m_tileH)}, origin);
            DrawRectOutline(r, hp,
                            {float(m_tileW) * m_zoom, float(m_tileH) * m_zoom},
                            2.0f, kHoverHi);
        }

        // Clip — visually mark the canvas border so it's clear where the
        // panel ends regardless of camera offset.
        DrawRectOutline(r, origin, size, 2.0f, kCanvasOut);
    }

    void DrawPalettePanel(Renderer2D& r) {
        const Vec2 origin{0.0f, kToolbarH};
        const Vec2 size  {kPaletteW, kScreenH - kToolbarH - kStatusH};
        r.DrawQuad(origin, size, kPaneBg);

        if (!m_tilesetTex || m_sheet.Count() <= 0) {
            DrawMicroText(r, "no tileset.",
                          {origin.x + 12.0f, origin.y + 12.0f},
                          12.0f, kStatusText);
            return;
        }

        float cellPx; int cols, rows;
        PaletteLayout(origin, size, cellPx, cols, rows);
        const float pad = 8.0f;
        const float pitch = cellPx + 4.0f;
        const int total = m_sheet.Count();
        for (int i = 0; i < total; ++i) {
            const int cx = i % cols;
            const int cy = i / cols;
            const float px = origin.x + pad + cx * pitch;
            const float py = origin.y + pad + cy * pitch - m_paletteScroll;
            if (py + cellPx < origin.y || py > origin.y + size.y) continue;
            const TextureRegion region = m_sheet.At(i);
            r.DrawQuad({px, py}, {cellPx, cellPx}, region);
            if (i == m_selectedTile) {
                DrawRectOutline(r, {px - 1.0f, py - 1.0f},
                                {cellPx + 2.0f, cellPx + 2.0f},
                                2.0f, kSelectHi);
            }
        }
    }

    void DrawToolbar(Renderer2D& r) {
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kToolbarH}, kToolbarBg);

        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "FILE: %s   TILESET: %s",
                      m_filePath.empty() ? "(unsaved)" : m_filePath.c_str(),
                      m_currentTilesetPath.empty()
                          ? "(none)" : m_currentTilesetPath.c_str());
        DrawMicroText(r, buf, {12.0f, 12.0f}, 14.0f, kStatusText);
    }

    void DrawStatusBar(Renderer2D& r) {
        const float y = kScreenH - kStatusH;
        r.DrawQuad({0.0f, y}, {kScreenW, kStatusH}, kToolbarBg);

        char buf[256];
        const int hx = m_hoverTx, hy = m_hoverTy;
        std::snprintf(buf, sizeof(buf),
                      "MAP: %dx%d   TILE: %dx%d   ZOOM: %.0f%%   "
                      "SEL: %d   HOVER: %d,%d   "
                      "LMB paint  RMB erase  MMB pan  WHEEL zoom  G grid  "
                      "CTRL+S save  CTRL+N new  CTRL+O reload",
                      m_map.Width(), m_map.Height(), m_tileW, m_tileH,
                      m_zoom * 100.0f, m_selectedTile,
                      hx, hy);
        DrawMicroText(r, buf, {12.0f, y + 8.0f}, 12.0f, kStatusText);
    }

    // ----- State -----
    Renderer2D                  m_renderer;
    EditorArgs                  m_args;
    std::unique_ptr<RHITexture> m_tilesetTex;
    SpriteSheet                 m_sheet;
    Tilemap                     m_map;
    std::string                 m_filePath;
    std::string                 m_currentTilesetPath;
    int   m_tileW = kDefaultTileSz;
    int   m_tileH = kDefaultTileSz;

    Vec2  m_camera{0.0f, 0.0f};
    float m_zoom = 1.0f;
    bool  m_grid = true;

    Vec2  m_panAnchor{0.0f, 0.0f};
    Vec2  m_camAnchor{0.0f, 0.0f};
    bool  m_panning = false;

    float m_paletteScroll = 0.0f;
    int   m_selectedTile = 0;
    int   m_hoverTx = -1;
    int   m_hoverTy = -1;
    bool  m_inCanvas = false;
    bool  m_inPalette = false;
};

Application* NaraGame::CreateApplication(int argc, char** argv) {
    return new EditorApp(ParseArgs(argc, argv));
}
