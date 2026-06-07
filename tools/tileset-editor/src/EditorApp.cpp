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
#include <deque>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  include <commdlg.h>
#  pragma comment(lib, "comdlg32.lib")
#endif

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

// ---------------------------------------------------------------------------
// Native file dialogs. Returns "" on cancel or platforms that don't have a
// backend yet. `filter` follows the Win32 `\0` double-terminated convention
// (e.g. L"PNG\0*.png\0\0"); we don't try to be portable past that.
// ---------------------------------------------------------------------------
std::string WideToUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string OpenFileDialog(const wchar_t* filter, const wchar_t* title) {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return WideToUtf8(buf);
#else
    (void)filter; (void)title;
    return {};
#endif
}

std::string SaveFileDialog(const wchar_t* filter, const wchar_t* title,
                           const wchar_t* defExt) {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.lpstrTitle  = title;
    ofn.lpstrDefExt = defExt;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return {};
    return WideToUtf8(buf);
#else
    (void)filter; (void)title; (void)defExt;
    return {};
#endif
}

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

        // Esc: clear an active selection first, only close the app when
        // there's nothing to dismiss. Lets the user back out of a marquee
        // without accidentally killing the editor.
        if (Input::IsKeyPressed(Key::Escape)) {
            if (m_hasSelection || m_selDragging) {
                m_hasSelection = false;
                m_selDragging  = false;
            } else {
                Close();
            }
        }

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
        if (Input::IsKeyPressed(Key::L) || Input::IsKeyPressed(Key::Tab)) {
            // 3-way cycle: Tile → Collision → Object → Tile.
            m_mode = static_cast<EditLayer>(
                (static_cast<int>(m_mode) + 1) % 3);
            if (m_mode == EditLayer::Collision) EnsureCollisionGrid();
        }
        // Home: recentre the canvas. Pretty much always the right "I got lost"
        // shortcut -- camera back to map origin, zoom back to 1:1.
        if (Input::IsKeyPressed(Key::Home)) {
            m_camera = {0.0f, 0.0f};
            m_zoom   = 1.0f;
        }
        // 1..9 jump-select the first nine palette entries in Tile mode (a
        // cheap power-user shortcut so you don't keep round-tripping to the
        // palette for the same handful of tiles). In Collision mode the
        // same digits pick Floor/Wall/Water.
        if (m_map.Width() > 0) {
            const Key digits[9] = {
                Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5,
                Key::Num6, Key::Num7, Key::Num8, Key::Num9,
            };
            for (int i = 0; i < 9; ++i) {
                if (!Input::IsKeyPressed(digits[i])) continue;
                if (m_mode == EditLayer::Tile) {
                    if (i < m_sheet.Count()) m_selectedTile = i;
                } else if (m_mode == EditLayer::Collision) {
                    if (i < 3) m_selectedCol = i;
                } else {
                    if (i < kObjectSymbolCount) m_selectedSym = kObjectSymbols[i];
                }
            }
        }
        if (ctrl && Input::IsKeyPressed(Key::C)) CopySelection();
        if (ctrl && Input::IsKeyPressed(Key::X)) {
            CopySelection();
            ClearSelectionCells();
        }
        if (ctrl && Input::IsKeyPressed(Key::V) &&
            m_hoverTx >= 0 && m_hoverTy >= 0) {
            PasteAt(m_hoverTx, m_hoverTy);
        }
        if (ctrl && Input::IsKeyPressed(Key::S) && m_map.Width() > 0) {
            // First save → pop a Save As dialog so the user can pick where
            // the .ntmap lands. After that Ctrl+S writes back to the same
            // path silently.
            if (m_filePath.empty()) {
                const std::string p = SaveFileDialog(
                    L"NaraGame tilemap (*.ntmap)\0*.ntmap\0All files\0*.*\0",
                    L"Save tilemap as", L"ntmap");
                if (!p.empty()) m_filePath = p;
            }
            if (!m_filePath.empty()) SaveCurrent();
        }
        if (ctrl && Input::IsKeyPressed(Key::O)) {
            const std::string p = OpenFileDialog(
                L"Tilemap (*.ntmap; *.tmx)\0*.ntmap;*.tmx\0"
                L"All files\0*.*\0",
                L"Open tilemap");
            if (!p.empty()) OpenPath(p);
        }
        if (ctrl && Input::IsKeyPressed(Key::N)) {
            // Pop a PNG picker for the tileset. The result starts a blank
            // map sized kDefaultMapW x kDefaultMapH at the existing tile
            // size (or kDefaultTileSz when nothing was loaded yet); the
            // user can resize with Ctrl+arrow afterwards.
            const std::string png = OpenFileDialog(
                L"PNG image (*.png)\0*.png\0All files\0*.*\0",
                L"Pick tileset PNG for new map");
            if (!png.empty()) {
                const int tw = m_tileW > 0 ? m_tileW : kDefaultTileSz;
                const int th = m_tileH > 0 ? m_tileH : kDefaultTileSz;
                StartNewMapWithTileset(png, tw, th,
                                       kDefaultMapW, kDefaultMapH);
                m_filePath = ReplaceExt(png, ".ntmap");
            }
        }

        // ----- Camera pan / resize -----
        // Ctrl claims the arrow keys for resize ops, so pan only runs when
        // Ctrl is up. Resize is edge-triggered (one tap = one row/col).
        if (ctrl) {
            const bool shrink = Input::IsKeyDown(Key::LeftShift);
            const int  d      = shrink ? -1 : +1;
            if (Input::IsKeyPressed(Key::Left))  DoResize(ResizeEdge::Left,   d);
            if (Input::IsKeyPressed(Key::Right)) DoResize(ResizeEdge::Right,  d);
            if (Input::IsKeyPressed(Key::Up))    DoResize(ResizeEdge::Top,    d);
            if (Input::IsKeyPressed(Key::Down))  DoResize(ResizeEdge::Bottom, d);
        } else {
            const float panSpeed = 400.0f / std::max(0.5f, m_zoom);
            if (Input::IsKeyDown(Key::Left))  m_camera.x -= panSpeed * dt;
            if (Input::IsKeyDown(Key::Right)) m_camera.x += panSpeed * dt;
            if (Input::IsKeyDown(Key::Up))    m_camera.y -= panSpeed * dt;
            if (Input::IsKeyDown(Key::Down))  m_camera.y += panSpeed * dt;
        }

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
        if (m_inPalette && Input::IsMousePressed(MouseButton::Left)) {
            if (m_mode == EditLayer::Tile && m_currentTilesetPath.size()) {
                const int picked = PaletteHit(mouse, paletteOrigin, paletteSize);
                if (picked >= 0) m_selectedTile = picked;
            } else if (m_mode == EditLayer::Collision) {
                const int picked = CollisionPaletteHit(mouse, paletteOrigin);
                if (picked >= 0) m_selectedCol = picked;
            } else if (m_mode == EditLayer::Object) {
                const int picked = ObjectPaletteHit(mouse, paletteOrigin);
                if (picked >= 0) m_selectedSym = kObjectSymbols[picked];
            }
        }

        // ----- Canvas paint / erase / fill / rect -----
        if (m_inCanvas && m_map.Width() > 0 && !m_panning) {
            const Vec2 world = ScreenToWorld(mouse, canvasOrigin, m_zoom);
            const int tx = int(std::floor(world.x / float(m_tileW)));
            const int ty = int(std::floor(world.y / float(m_tileH)));
            m_hoverTx = tx;
            m_hoverTy = ty;
            const bool inMap = tx >= 0 && ty >= 0 &&
                               tx < m_map.Width() && ty < m_map.Height();
            // Mode-dependent values:
            //   Tile     → LMB paints m_selectedTile, RMB clears (-1)
            //   Collision→ LMB paints m_selectedCol,  RMB clears (0=Floor)
            //   Object   → LMB paints m_selectedSym,  RMB clears (0=remove)
            int leftVal = 0, rightVal = 0;
            switch (m_mode) {
                case EditLayer::Tile:
                    leftVal = m_selectedTile; rightVal = -1; break;
                case EditLayer::Collision:
                    leftVal = m_selectedCol;  rightVal = 0;  break;
                case EditLayer::Object:
                    leftVal = int(uint8_t(m_selectedSym));
                    rightVal = 0;  break;
            }
            const bool shift = Input::IsKeyDown(Key::LeftShift);
            const bool alt   = Input::IsKeyDown(Key::LeftAlt);

            // Eyedropper (Alt + LMB). Picks the hover cell's value into the
            // active selection so the user can re-paint with it. While Alt
            // is held we suppress fill / rect / paint entirely -- a missed
            // pick shouldn't splat the previous selection over the source.
            if (alt) {
                if (inMap && Input::IsMousePressed(MouseButton::Left)) {
                    if (m_mode == EditLayer::Tile) {
                        const int id = m_map.GetTile(tx, ty);
                        if (id >= 0) m_selectedTile = id;
                    } else if (!m_collision.empty()) {
                        m_selectedCol = int(
                            m_collision[size_t(ty) * m_map.Width()
                                        + size_t(tx)]);
                    }
                }
            } else if (ctrl) {
                // Ctrl + LMB drag: marquee selection. Press latches the
                // anchor; while held the rectangle tracks the cursor;
                // release leaves m_hasSelection set so Ctrl+C / V / X can
                // grab it. Ctrl is also bound to grow/shrink (arrows) and
                // many other shortcuts, but those are key events and don't
                // collide with mouse drags.
                if (inMap && Input::IsMousePressed(MouseButton::Left)) {
                    m_selDragging  = true;
                    m_hasSelection = true;
                    m_selAnchorX   = tx;
                    m_selAnchorY   = ty;
                    m_selX0 = tx; m_selX1 = tx;
                    m_selY0 = ty; m_selY1 = ty;
                }
                if (m_selDragging) {
                    if (!Input::IsMouseDown(MouseButton::Left)) {
                        m_selDragging = false;
                    } else {
                        const int ex = std::clamp(tx, 0, m_map.Width()  - 1);
                        const int ey = std::clamp(ty, 0, m_map.Height() - 1);
                        m_selX0 = std::min(m_selAnchorX, ex);
                        m_selX1 = std::max(m_selAnchorX, ex);
                        m_selY0 = std::min(m_selAnchorY, ey);
                        m_selY1 = std::max(m_selAnchorY, ey);
                    }
                }
            } else {
                // Fill bucket — F at any time fills the contiguous region
                // of the cell under the cursor. Same-value fills are
                // no-ops; Undo treats the whole sweep as one stroke.
                if (inMap && Input::IsKeyPressed(Key::F)) {
                    FloodFill(tx, ty, leftVal);
                    EndStroke();
                }

                // Rectangle: shift + LMB/RMB press latches the anchor;
                // release commits. Shift can be let go mid-drag (latched).
                if (inMap && shift &&
                    Input::IsMousePressed(MouseButton::Left)) {
                    m_rectActive = true;
                    m_rectAnchorX = tx;
                    m_rectAnchorY = ty;
                    m_rectLastButton = 0;
                } else if (inMap && shift &&
                           Input::IsMousePressed(MouseButton::Right)) {
                    m_rectActive = true;
                    m_rectAnchorX = tx;
                    m_rectAnchorY = ty;
                    m_rectLastButton = 1;
                }

                if (m_rectActive) {
                    const bool stillDownL = Input::IsMouseDown(MouseButton::Left);
                    const bool stillDownR = Input::IsMouseDown(MouseButton::Right);
                    const bool stillDown = (m_rectLastButton == 0)
                                               ? stillDownL : stillDownR;
                    if (!stillDown) {
                        const int ex = std::clamp(tx, 0, m_map.Width()  - 1);
                        const int ey = std::clamp(ty, 0, m_map.Height() - 1);
                        const int x0 = std::min(m_rectAnchorX, ex);
                        const int x1 = std::max(m_rectAnchorX, ex);
                        const int y0 = std::min(m_rectAnchorY, ey);
                        const int y1 = std::max(m_rectAnchorY, ey);
                        const int v  = (m_rectLastButton == 0) ? leftVal : rightVal;
                        for (int yy = y0; yy <= y1; ++yy)
                            for (int xx = x0; xx <= x1; ++xx)
                                PaintCell(xx, yy, v);
                        EndStroke();
                        m_rectActive = false;
                    }
                } else if (inMap) {
                    if (Input::IsMouseDown(MouseButton::Left)) {
                        PaintCell(tx, ty, leftVal);
                    } else if (Input::IsMouseDown(MouseButton::Right)) {
                        PaintCell(tx, ty, rightVal);
                    }
                }
            }
        } else {
            m_hoverTx = -1;
            m_hoverTy = -1;
        }
        // Mouse fully up → finalise the current stroke. Doing this after the
        // paint pass means a single click that flips one cell still becomes a
        // 1-edit stroke pushed onto the undo stack.
        if (m_currentStroke &&
            !Input::IsMouseDown(MouseButton::Left) &&
            !Input::IsMouseDown(MouseButton::Right)) {
            EndStroke();
        }

        // ----- Undo / Redo -----
        // Ctrl+Z undo. Ctrl+Y or Ctrl+Shift+Z redo (Photoshop+Windows
        // conventions). Edits made mid-drag belong to a stroke that hasn't
        // landed on the stack yet, so finalise it first so undo grabs the
        // most recent finished operation.
        if (ctrl && Input::IsKeyPressed(Key::Z)) {
            if (m_currentStroke) EndStroke();
            const bool shift = Input::IsKeyDown(Key::LeftShift);
            if (shift) Redo(); else Undo();
        }
        if (ctrl && Input::IsKeyPressed(Key::Y)) {
            if (m_currentStroke) EndStroke();
            Redo();
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

    // ----- Undo / Redo -----
    //
    // Edit granularity = one mouse-drag = one Stroke. A Stroke holds the
    // before/after value for every cell it changed plus which layer the
    // edit landed on (0 = tile id, 1 = collision code). One stroke can
    // freely mix both layers, but in practice a drag never switches modes
    // mid-way -- this just keeps the undo stack honest if it ever does.
    enum class EditLayer : int { Tile = 0, Collision = 1, Object = 2 };
    struct StrokeEdit { EditLayer layer; int x, y, oldId, newId; };

    // Resize is too coarse to express as cell edits -- one operation can
    // change the dimensions and shift every cell. Stash whole-grid before /
    // after snapshots in a Stroke and undo by restoring the old shape.
    struct ResizeSnapshot {
        int                       oldW = 0, oldH = 0;
        std::vector<int>          oldTiles;
        std::vector<uint8_t>      oldCol;
        std::vector<ObjectMarker> oldObjects;
        int                       newW = 0, newH = 0;
        std::vector<int>          newTiles;
        std::vector<uint8_t>      newCol;
        std::vector<ObjectMarker> newObjects;
    };
    struct Stroke {
        std::vector<StrokeEdit>         edits;
        std::unique_ptr<ResizeSnapshot> resize;   // null for normal strokes
    };
    static constexpr size_t kUndoLimit = 200;

    // Paint to the currently active layer. Routes to either the visual
    // Tilemap, the parallel collision grid, or the object marker list
    // based on m_mode. For objects, newId is the symbol char (0 = no
    // marker / erase).
    void PaintCell(int x, int y, int newId) {
        if (m_map.Width() <= 0) return;
        if (m_mode == EditLayer::Tile) {
            const int oldId = m_map.GetTile(x, y);
            if (oldId == newId) return;
            RecordEdit(EditLayer::Tile, x, y, oldId, newId);
            m_map.SetTile(x, y, newId);
        } else if (m_mode == EditLayer::Collision) {
            EnsureCollisionGrid();
            const size_t idx = size_t(y) * m_map.Width() + size_t(x);
            const int oldId = int(m_collision[idx]);
            if (oldId == newId) return;
            RecordEdit(EditLayer::Collision, x, y, oldId, newId);
            m_collision[idx] = uint8_t(newId);
        } else {
            const int oldSym = GetObjectSymbol(x, y);
            if (oldSym == newId) return;
            RecordEdit(EditLayer::Object, x, y, oldSym, newId);
            SetObjectSymbol(x, y, char(newId));
        }
        m_dirty = true;
    }

    // Object marker helpers. Markers live in a flat std::vector; at most
    // one per cell so writes are insert-or-replace and 0 means "remove".
    int GetObjectSymbol(int x, int y) const {
        for (const auto& m : m_objects)
            if (m.x == x && m.y == y) return int(uint8_t(m.symbol));
        return 0;
    }
    void SetObjectSymbol(int x, int y, char sym) {
        for (auto it = m_objects.begin(); it != m_objects.end(); ++it) {
            if (it->x == x && it->y == y) {
                if (sym == 0) m_objects.erase(it);
                else it->symbol = sym;
                return;
            }
        }
        if (sym != 0) m_objects.push_back({sym, x, y});
    }

    void RecordEdit(EditLayer layer, int x, int y, int oldId, int newId) {
        BeginStroke();
        const uint64_t key = (uint64_t(int(layer)) << 62) |
                             (uint64_t(uint32_t(y)) << 32) | uint32_t(x);
        if (m_currentTouched.insert(key).second) {
            m_currentStroke->edits.push_back({layer, x, y, oldId, newId});
        }
    }

    void EnsureCollisionGrid() {
        const size_t need = size_t(m_map.Width()) * size_t(m_map.Height());
        if (m_collision.size() != need) m_collision.assign(need, 0);
    }

    void BeginStroke() {
        if (m_currentStroke) return;
        m_currentStroke = std::make_unique<Stroke>();
        m_currentTouched.clear();
    }

    void EndStroke() {
        if (!m_currentStroke) return;
        if (!m_currentStroke->edits.empty()) {
            m_undoStack.push_back(std::move(*m_currentStroke));
            if (m_undoStack.size() > kUndoLimit) m_undoStack.pop_front();
            m_redoStack.clear();          // any new edit kills the redo trail
        }
        m_currentStroke.reset();
        m_currentTouched.clear();
    }

    void ApplyEdit(const StrokeEdit& e, bool undo) {
        const int v = undo ? e.oldId : e.newId;
        if (e.layer == EditLayer::Tile) {
            m_map.SetTile(e.x, e.y, v);
        } else if (e.layer == EditLayer::Collision) {
            EnsureCollisionGrid();
            m_collision[size_t(e.y) * m_map.Width() + size_t(e.x)] = uint8_t(v);
        } else {
            SetObjectSymbol(e.x, e.y, char(v));
        }
    }

    // Swap the whole map/collision out for the supplied snapshot. Used by
    // both DoResize (forward) and Undo/Redo (backward/forward); keeps the
    // SpriteSheet binding the editor already had so palette / drawing keep
    // working without re-binding tileset state.
    void ApplyResizeSnapshot(int w, int h,
                             const std::vector<int>& tiles,
                             const std::vector<uint8_t>& col,
                             const std::vector<ObjectMarker>& objs) {
        Tilemap newMap(w, h, float(m_tileW), float(m_tileH));
        newMap.SetTileset(m_sheet);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                newMap.SetTile(x, y, tiles[size_t(y) * size_t(w) + size_t(x)]);
        m_map       = std::move(newMap);
        m_collision = col;
        m_objects   = objs;
    }

    void Undo() {
        if (m_undoStack.empty()) return;
        Stroke s = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        if (s.resize) {
            ApplyResizeSnapshot(s.resize->oldW, s.resize->oldH,
                                s.resize->oldTiles, s.resize->oldCol,
                                s.resize->oldObjects);
        } else {
            // Apply in reverse so overlapping edits inside one stroke wind
            // back to the original baseline rather than to an intermediate
            // state.
            for (auto it = s.edits.rbegin(); it != s.edits.rend(); ++it) {
                ApplyEdit(*it, /*undo=*/true);
            }
        }
        m_redoStack.push_back(std::move(s));
        m_dirty = true;
    }

    void Redo() {
        if (m_redoStack.empty()) return;
        Stroke s = std::move(m_redoStack.back());
        m_redoStack.pop_back();
        if (s.resize) {
            ApplyResizeSnapshot(s.resize->newW, s.resize->newH,
                                s.resize->newTiles, s.resize->newCol,
                                s.resize->newObjects);
        } else {
            for (auto& e : s.edits) {
                ApplyEdit(e, /*undo=*/false);
            }
        }
        m_undoStack.push_back(std::move(s));
        m_dirty = true;
    }

    void ClearHistory() {
        m_undoStack.clear();
        m_redoStack.clear();
        m_currentStroke.reset();
        m_currentTouched.clear();
        m_dirty = false;
    }

    // ----- Selection clipboard -----
    //
    // Copy reads the selection rectangle from the active layer and stashes
    // a snapshot in m_clipboard. Paste lays the snapshot down at (tx, ty),
    // clipping at the map's far edges so a paste near the corner doesn't
    // wrap. Both ops run through PaintCell so they land on the undo stack
    // as a single stroke.
    void CopySelection() {
        if (!m_hasSelection || m_map.Width() <= 0) return;
        const int w = m_selX1 - m_selX0 + 1;
        const int h = m_selY1 - m_selY0 + 1;
        if (w <= 0 || h <= 0) return;
        m_clipboard.layer = m_mode;
        m_clipboard.w     = w;
        m_clipboard.h     = h;
        m_clipboard.data.assign(size_t(w) * size_t(h), 0);
        for (int yy = 0; yy < h; ++yy) {
            for (int xx = 0; xx < w; ++xx) {
                int v = 0;
                if (m_mode == EditLayer::Tile) {
                    v = m_map.GetTile(m_selX0 + xx, m_selY0 + yy);
                } else {
                    EnsureCollisionGrid();
                    v = int(m_collision[size_t(m_selY0 + yy) * m_map.Width()
                                        + size_t(m_selX0 + xx)]);
                }
                m_clipboard.data[size_t(yy) * w + xx] = v;
            }
        }
    }

    void PasteAt(int tx, int ty) {
        if (m_clipboard.w <= 0 || m_map.Width() <= 0) return;
        if (m_clipboard.layer != m_mode) {
            NARAGAME_INFO("editor: paste skipped (clipboard layer mismatch -- "
                          "switch with TAB)");
            return;
        }
        BeginStroke();
        for (int yy = 0; yy < m_clipboard.h; ++yy) {
            for (int xx = 0; xx < m_clipboard.w; ++xx) {
                const int dx = tx + xx;
                const int dy = ty + yy;
                if (dx < 0 || dy < 0 ||
                    dx >= m_map.Width() || dy >= m_map.Height())
                    continue;
                PaintCell(dx, dy,
                          m_clipboard.data[size_t(yy) * m_clipboard.w + xx]);
            }
        }
        EndStroke();
    }

    // Cut: copy then wipe the selection on the active layer. Wipe runs as
    // its own stroke (separate from the copy, which doesn't change the
    // map), so Undo backs out the wipe and leaves the clipboard alone.
    void ClearSelectionCells() {
        if (!m_hasSelection || m_map.Width() <= 0) return;
        const int empty = (m_mode == EditLayer::Tile) ? -1 : 0;
        BeginStroke();
        for (int yy = m_selY0; yy <= m_selY1; ++yy)
            for (int xx = m_selX0; xx <= m_selX1; ++xx)
                PaintCell(xx, yy, empty);
        EndStroke();
    }

    // Map resize. delta > 0 grows by one row/col on the given edge; delta < 0
    // shrinks. Shrinking past 1 in either axis is a no-op (a zero-area map
    // breaks every other path). When growing, the new cells are -1 / 0
    // (empty tile / Floor); when shrinking, dropped cells are kept in the
    // ResizeSnapshot so Undo can put them back.
    enum class ResizeEdge { Left, Right, Top, Bottom };

    void DoResize(ResizeEdge edge, int delta) {
        if (m_map.Width() <= 0 || delta == 0) return;
        const int W = m_map.Width();
        const int H = m_map.Height();
        int newW = W, newH = H;
        switch (edge) {
            case ResizeEdge::Left:
            case ResizeEdge::Right:  newW = W + delta; break;
            case ResizeEdge::Top:
            case ResizeEdge::Bottom: newH = H + delta; break;
        }
        if (newW < 1 || newH < 1) return;

        const bool hasCol = (m_collision.size() == size_t(W) * size_t(H));
        std::vector<int>     oldTiles(size_t(W) * size_t(H), -1);
        std::vector<uint8_t> oldCol;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                oldTiles[size_t(y) * W + x] = m_map.GetTile(x, y);
        if (hasCol) oldCol = m_collision;

        // src(x, y) — index from the OLD grid that the NEW cell (x, y) takes
        // its value from. Returns -1 if there's no source (newly added edge).
        // For shrink ops we just shift the window; for grow ops we offset.
        auto src = [&](int x, int y, int& sx, int& sy) {
            sx = x; sy = y;
            switch (edge) {
                case ResizeEdge::Left:   sx = x - delta; break;
                case ResizeEdge::Right:  /* sx = x */     break;
                case ResizeEdge::Top:    sy = y - delta; break;
                case ResizeEdge::Bottom: /* sy = y */     break;
            }
        };

        std::vector<int>     newTiles(size_t(newW) * size_t(newH), -1);
        std::vector<uint8_t> newCol;
        if (hasCol) newCol.assign(size_t(newW) * size_t(newH), 0);
        for (int y = 0; y < newH; ++y) {
            for (int x = 0; x < newW; ++x) {
                int sx, sy;
                src(x, y, sx, sy);
                if (sx >= 0 && sy >= 0 && sx < W && sy < H) {
                    newTiles[size_t(y) * newW + x] = oldTiles[size_t(sy) * W + sx];
                    if (hasCol)
                        newCol[size_t(y) * newW + x] = oldCol[size_t(sy) * W + sx];
                }
            }
        }

        // Object markers shift by the same delta as the cells on a grow
        // edge / shift inward on a shrink edge; anything that falls off
        // the new bounds is dropped (Undo's snapshot keeps the originals).
        std::vector<ObjectMarker> oldObjects = m_objects;
        std::vector<ObjectMarker> newObjects;
        newObjects.reserve(oldObjects.size());
        for (const auto& m : oldObjects) {
            int mx = m.x, my = m.y;
            switch (edge) {
                case ResizeEdge::Left:   mx += delta; break;
                case ResizeEdge::Right:                break;
                case ResizeEdge::Top:    my += delta; break;
                case ResizeEdge::Bottom:               break;
            }
            if (mx >= 0 && my >= 0 && mx < newW && my < newH)
                newObjects.push_back({m.symbol, mx, my});
        }

        // Push a resize-only stroke onto the undo stack and clear redo.
        // Any in-flight paint stroke is finalised first so the resize
        // doesn't bury an un-pushed paint operation.
        if (m_currentStroke) EndStroke();
        Stroke s;
        s.resize = std::make_unique<ResizeSnapshot>();
        s.resize->oldW = W;     s.resize->oldH = H;
        s.resize->oldTiles   = std::move(oldTiles);
        s.resize->oldCol     = std::move(oldCol);
        s.resize->oldObjects = std::move(oldObjects);
        s.resize->newW = newW;  s.resize->newH = newH;
        s.resize->newTiles   = newTiles;
        s.resize->newCol     = newCol;
        s.resize->newObjects = newObjects;
        ApplyResizeSnapshot(newW, newH, newTiles, newCol, newObjects);
        m_undoStack.push_back(std::move(s));
        if (m_undoStack.size() > kUndoLimit) m_undoStack.pop_front();
        m_redoStack.clear();
        m_dirty = true;
    }

    // Paint-bucket fill. 4-connected BFS over the active layer; only cells
    // whose current value matches the seed's get repainted. Wrapping all
    // PaintCell calls in one BeginStroke / EndStroke pair lets Undo collapse
    // an entire fill into one step.
    void FloodFill(int sx, int sy, int newId) {
        if (m_map.Width() <= 0) return;
        if (sx < 0 || sy < 0 || sx >= m_map.Width() || sy >= m_map.Height())
            return;
        // Read seed from the active layer; bail if no change would happen.
        int seedId = 0;
        if (m_mode == EditLayer::Tile) {
            seedId = m_map.GetTile(sx, sy);
        } else {
            EnsureCollisionGrid();
            seedId = int(m_collision[size_t(sy) * m_map.Width() + size_t(sx)]);
        }
        if (seedId == newId) return;

        const int W = m_map.Width(), H = m_map.Height();
        std::vector<uint8_t> visited(size_t(W) * size_t(H), 0);
        std::vector<std::pair<int,int>> stack;
        stack.reserve(64);
        stack.emplace_back(sx, sy);
        visited[size_t(sy) * W + size_t(sx)] = 1;
        BeginStroke();
        while (!stack.empty()) {
            const auto [x, y] = stack.back();
            stack.pop_back();
            // Re-check the value in case an earlier edit in this same fill
            // already changed it (shouldn't happen with the visited guard,
            // but keeps the invariant honest if PaintCell is ever extended).
            int cur = 0;
            if (m_mode == EditLayer::Tile)
                cur = m_map.GetTile(x, y);
            else
                cur = int(m_collision[size_t(y) * W + size_t(x)]);
            if (cur != seedId) continue;
            PaintCell(x, y, newId);
            const int dx[4] = { 1, -1, 0, 0 };
            const int dy[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dx[k];
                const int ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                const size_t i = size_t(ny) * W + size_t(nx);
                if (visited[i]) continue;
                visited[i] = 1;
                stack.emplace_back(nx, ny);
            }
        }
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
        m_collision  = std::move(asset.collision);   // empty if none in file
        m_objects    = std::move(asset.objects);     // empty if none in file
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
        ClearHistory();
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
        m_collision.clear();
        m_objects.clear();
        m_currentTilesetPath = tilesetPath;
        m_camera = {0.0f, 0.0f};
        m_zoom   = 1.0f;
        m_paletteScroll = 0.0f;
        m_selectedTile  = 0;
        ClearHistory();
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
        // Only round-trip collision when it has the right shape. An empty
        // grid means "no collision data" and SaveNtmap will skip the section.
        const size_t need = size_t(m_map.Width()) * size_t(m_map.Height());
        const std::vector<uint8_t> emptyCol;
        const bool ok = SaveNtmap(out, m_map, m_currentTilesetPath,
                                  m_tileW, m_tileH, 0, 0,
                                  m_collision.size() == need ? m_collision
                                                             : emptyCol,
                                  m_objects);
        if (ok) {
            m_filePath = out;
            m_dirty = false;
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

        // Collision overlay. Visible in BOTH modes so authors can always see
        // what they've marked as solid -- collision-mode just makes the
        // overlay the primary thing they're editing.
        if (!m_collision.empty()) {
            const Color cWall (0.95f, 0.30f, 0.30f, 0.42f);
            const Color cWater(0.25f, 0.55f, 0.95f, 0.42f);
            for (int ty = 0; ty < m_map.Height(); ++ty) {
                for (int tx = 0; tx < m_map.Width(); ++tx) {
                    const uint8_t c = m_collision[size_t(ty) * m_map.Width()
                                                  + size_t(tx)];
                    if (c == 0) continue;
                    const Vec2 p = WorldToScreen(
                        {float(tx * m_tileW), float(ty * m_tileH)}, origin);
                    r.DrawQuad(p, {float(m_tileW) * m_zoom,
                                    float(m_tileH) * m_zoom},
                               c == 1 ? cWall : cWater);
                }
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

        // Object markers. Drawn over both tile and collision layers so the
        // user can keep an eye on spawn placement while editing anything.
        if (!m_objects.empty()) {
            const float sx = float(m_tileW) * m_zoom;
            const float sy = float(m_tileH) * m_zoom;
            for (const auto& m : m_objects) {
                const Vec2 p = WorldToScreen(
                    {float(m.x * m_tileW), float(m.y * m_tileH)}, origin);
                r.DrawQuad(p, {sx, sy}, Color(0.0f, 0.0f, 0.0f, 0.55f));
                DrawRectOutline(r, p, {sx, sy}, 2.0f,
                                Color(1.0f, 0.86f, 0.20f, 1.0f));
                const char text[2] = { m.symbol, 0 };
                const float gh = std::min(sx, sy) * 0.6f;
                const float w  = MeasureMicroText(text, gh);
                DrawMicroText(r, text,
                              {p.x + (sx - w) * 0.5f,
                               p.y + (sy - gh) * 0.5f},
                              gh, Color(1.0f, 0.95f, 0.30f, 1.0f));
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

        // Persistent marquee selection. Drawn whenever m_hasSelection is
        // true, regardless of mode, so the user can switch layers / pan /
        // zoom and the selection stays anchored.
        if (m_hasSelection) {
            const Vec2 a = WorldToScreen(
                {float(m_selX0 * m_tileW), float(m_selY0 * m_tileH)}, origin);
            const Vec2 b = WorldToScreen(
                {float((m_selX1 + 1) * m_tileW),
                 float((m_selY1 + 1) * m_tileH)}, origin);
            const Vec2 sz{b.x - a.x, b.y - a.y};
            r.DrawQuad(a, sz, Color(1.0f, 1.0f, 1.0f, 0.10f));
            DrawRectOutline(r, a, sz, 2.0f, Color(1.0f, 1.0f, 1.0f, 0.95f));
        }

        // Rectangle marquee preview while shift-dragging. Drawn last so it
        // sits on top of grid + hover; commit happens on mouse release.
        if (m_rectActive && m_hoverTx >= 0 && m_hoverTy >= 0) {
            const int ex = std::clamp(m_hoverTx, 0, m_map.Width()  - 1);
            const int ey = std::clamp(m_hoverTy, 0, m_map.Height() - 1);
            const int x0 = std::min(m_rectAnchorX, ex);
            const int x1 = std::max(m_rectAnchorX, ex);
            const int y0 = std::min(m_rectAnchorY, ey);
            const int y1 = std::max(m_rectAnchorY, ey);
            const Vec2 a = WorldToScreen(
                {float(x0 * m_tileW), float(y0 * m_tileH)}, origin);
            const Vec2 b = WorldToScreen(
                {float((x1 + 1) * m_tileW), float((y1 + 1) * m_tileH)}, origin);
            const Vec2 sz{b.x - a.x, b.y - a.y};
            r.DrawQuad(a, sz, Color(1.0f, 0.86f, 0.20f, 0.20f));
            DrawRectOutline(r, a, sz, 2.0f, kSelectHi);
        }

        // Clip — visually mark the canvas border so it's clear where the
        // panel ends regardless of camera offset.
        DrawRectOutline(r, origin, size, 2.0f, kCanvasOut);
    }

    void DrawPalettePanel(Renderer2D& r) {
        const Vec2 origin{0.0f, kToolbarH};
        const Vec2 size  {kPaletteW, kScreenH - kToolbarH - kStatusH};
        r.DrawQuad(origin, size, kPaneBg);

        if (m_mode == EditLayer::Collision) {
            DrawCollisionPalette(r, origin);
            return;
        }
        if (m_mode == EditLayer::Object) {
            DrawObjectPalette(r, origin);
            return;
        }

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

    // Three big swatches for the collision modes. Same hit math as
    // CollisionPaletteHit so layout edits stay in lockstep.
    void DrawCollisionPalette(Renderer2D& r, Vec2 origin) {
        DrawMicroText(r, "collision layer", {origin.x + 12.0f, origin.y + 8.0f},
                      14.0f, kStatusText);
        struct Item { const char* label; Color color; };
        const Item items[3] = {
            { "floor", Color(0.50f, 0.55f, 0.60f, 0.50f) },
            { "wall",  Color(0.95f, 0.30f, 0.30f, 0.65f) },
            { "water", Color(0.25f, 0.55f, 0.95f, 0.65f) },
        };
        constexpr float pad   = 12.0f;
        constexpr float cellH = 56.0f;
        constexpr float gap   = 8.0f;
        for (int i = 0; i < 3; ++i) {
            const float py = origin.y + 32.0f + i * (cellH + gap);
            const Vec2 p{origin.x + pad, py};
            const Vec2 sz{kPaletteW - pad * 2.0f, cellH};
            r.DrawQuad(p, sz, items[i].color);
            DrawRectOutline(r, p, sz, 1.0f, Color(0.0f, 0.0f, 0.0f, 0.6f));
            DrawMicroText(r, items[i].label,
                          {p.x + 10.0f, p.y + cellH * 0.5f - 8.0f},
                          18.0f, kStatusText);
            if (i == m_selectedCol) {
                DrawRectOutline(r, {p.x - 2.0f, p.y - 2.0f},
                                {sz.x + 4.0f, sz.y + 4.0f}, 2.0f, kSelectHi);
            }
        }
    }

    int CollisionPaletteHit(Vec2 mouse, Vec2 origin) const {
        constexpr float pad   = 12.0f;
        constexpr float cellH = 56.0f;
        constexpr float gap   = 8.0f;
        for (int i = 0; i < 3; ++i) {
            const float py = origin.y + 32.0f + i * (cellH + gap);
            const float px = origin.x + pad;
            if (mouse.x >= px && mouse.x < px + (kPaletteW - pad * 2.0f) &&
                mouse.y >= py && mouse.y < py + cellH) {
                return i;
            }
        }
        return -1;
    }

    // Object marker palette: a fixed roster of single-character symbols.
    // The brawl integration reads 'P' as the player spawn and 'E' as an
    // enemy spawn; the other entries are placeholders games can claim.
    static constexpr char  kObjectSymbols[]    = { 'P', 'E', 'I', 'C', 'X' };
    static constexpr int   kObjectSymbolCount  = sizeof(kObjectSymbols)
                                                 / sizeof(kObjectSymbols[0]);

    void DrawObjectPalette(Renderer2D& r, Vec2 origin) {
        DrawMicroText(r, "object layer",
                      {origin.x + 12.0f, origin.y + 8.0f},
                      14.0f, kStatusText);
        constexpr float pad   = 12.0f;
        constexpr float cellH = 56.0f;
        constexpr float gap   = 8.0f;
        for (int i = 0; i < kObjectSymbolCount; ++i) {
            const float py = origin.y + 32.0f + i * (cellH + gap);
            const Vec2 p{origin.x + pad, py};
            const Vec2 sz{kPaletteW - pad * 2.0f, cellH};
            r.DrawQuad(p, sz, Color(0.20f, 0.22f, 0.30f, 1.0f));
            DrawRectOutline(r, p, sz, 1.0f, Color(0.0f, 0.0f, 0.0f, 0.6f));
            const char text[2] = { kObjectSymbols[i], 0 };
            DrawMicroText(r, text,
                          {p.x + 12.0f, p.y + cellH * 0.5f - 12.0f},
                          24.0f, kSelectHi);
            DrawMicroText(r, ObjectLabel(kObjectSymbols[i]),
                          {p.x + 50.0f, p.y + cellH * 0.5f - 8.0f},
                          14.0f, kStatusText);
            if (kObjectSymbols[i] == m_selectedSym) {
                DrawRectOutline(r, {p.x - 2.0f, p.y - 2.0f},
                                {sz.x + 4.0f, sz.y + 4.0f},
                                2.0f, kSelectHi);
            }
        }
    }

    int ObjectPaletteHit(Vec2 mouse, Vec2 origin) const {
        constexpr float pad   = 12.0f;
        constexpr float cellH = 56.0f;
        constexpr float gap   = 8.0f;
        for (int i = 0; i < kObjectSymbolCount; ++i) {
            const float py = origin.y + 32.0f + i * (cellH + gap);
            const float px = origin.x + pad;
            if (mouse.x >= px && mouse.x < px + (kPaletteW - pad * 2.0f) &&
                mouse.y >= py && mouse.y < py + cellH) {
                return i;
            }
        }
        return -1;
    }

    static const char* ObjectLabel(char sym) {
        switch (sym) {
            case 'P': return "player";
            case 'E': return "enemy";
            case 'I': return "item";
            case 'C': return "coin";
            default:  return "marker";
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
        const char* modeName = (m_mode == EditLayer::Tile)      ? "TILE"
                             : (m_mode == EditLayer::Collision) ? "COLLISION"
                                                                : "OBJECT";
        const int   selVal   = (m_mode == EditLayer::Tile)      ? m_selectedTile
                             : (m_mode == EditLayer::Collision) ? m_selectedCol
                                                                : int(m_selectedSym);
        std::snprintf(buf, sizeof(buf),
                      "MAP: %dx%d   TILE: %dx%d   ZOOM: %.0f%%   "
                      "MODE: %s   SEL: %d   HOVER: %d,%d   "
                      "UNDO: %zu/%zu   %s   "
                      "LMB paint  RMB erase  ALT+click pick  "
                      "SHIFT+drag rect  CTRL+drag select  F fill  "
                      "MMB pan  WHEEL zoom  HOME reset view  "
                      "1-9 quick select  G grid  TAB/L mode  ESC clear sel  "
                      "CTRL+ARROW grow  CTRL+SHIFT+ARROW shrink  "
                      "CTRL+C copy  CTRL+V paste  CTRL+X cut  "
                      "CTRL+Z undo  CTRL+Y redo  CTRL+S save  "
                      "CTRL+N new  CTRL+O open",
                      m_map.Width(), m_map.Height(), m_tileW, m_tileH,
                      m_zoom * 100.0f, modeName, selVal,
                      hx, hy, m_undoStack.size(), m_redoStack.size(),
                      m_dirty ? "MODIFIED" : "SAVED");
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

    // Undo / redo: completed strokes pushed onto m_undoStack; Undo() moves
    // them to m_redoStack. m_currentStroke is the active drag still
    // accumulating edits; m_currentTouched dedups cells within a stroke so
    // re-painting the same cell mid-drag doesn't overwrite the oldId
    // baseline. m_dirty marks "unsaved changes since last save/load".
    std::deque<Stroke>          m_undoStack;
    std::deque<Stroke>          m_redoStack;
    std::unique_ptr<Stroke>     m_currentStroke;
    std::unordered_set<uint64_t> m_currentTouched;
    bool m_dirty = false;

    // Collision layer. Parallel uint8_t grid alongside the visual Tilemap;
    // 0=Floor, 1=Wall, 2=Water (matching the engine's Collision::* codes).
    // Empty until the user enters Collision mode or a .ntmap with a
    // `collision` section is loaded.
    std::vector<uint8_t>      m_collision;
    std::vector<ObjectMarker> m_objects;
    EditLayer                 m_mode         = EditLayer::Tile;
    int                       m_selectedCol  = 1; // default Wall in collision mode
    char                      m_selectedSym  = 'P'; // default player spawn

    // Rectangle paint state. Latched when the user pressed LMB while holding
    // shift; cleared when LMB is released. While latched the per-cell paint
    // loop is bypassed and the canvas draws a marquee instead -- the
    // rectangle is only committed on release, as one stroke for undo.
    bool m_rectActive  = false;
    int  m_rectAnchorX = 0;
    int  m_rectAnchorY = 0;
    int  m_rectLastButton = 0;  // 0 = LMB, 1 = RMB; remembered across the drag

    // Marquee selection state. Ctrl + LMB drag sets up the anchor and
    // updates the rectangle each frame; release latches m_hasSelection.
    // Esc clears it. Ctrl+C / Ctrl+V / Ctrl+X operate on it.
    bool m_hasSelection = false;
    bool m_selDragging  = false;
    int  m_selAnchorX   = 0;
    int  m_selAnchorY   = 0;
    int  m_selX0 = 0, m_selY0 = 0;
    int  m_selX1 = 0, m_selY1 = 0;

    // Clipboard. Stores whichever layer the copy came from; paste refuses
    // to apply across layers (mismatched data wouldn't make sense).
    struct Clipboard {
        EditLayer        layer = EditLayer::Tile;
        int              w = 0, h = 0;
        std::vector<int> data;   // tile ids OR collision codes per layer
    };
    Clipboard m_clipboard;
};

Application* NaraGame::CreateApplication(int argc, char** argv) {
    return new EditorApp(ParseArgs(argc, argv));
}
