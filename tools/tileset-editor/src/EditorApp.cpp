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
        if (Input::IsKeyPressed(Key::L)) {
            m_mode = (m_mode == EditLayer::Tile) ? EditLayer::Collision
                                                 : EditLayer::Tile;
            if (m_mode == EditLayer::Collision) EnsureCollisionGrid();
        }
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
        if (m_inPalette && Input::IsMousePressed(MouseButton::Left)) {
            if (m_mode == EditLayer::Tile && m_currentTilesetPath.size()) {
                const int picked = PaletteHit(mouse, paletteOrigin, paletteSize);
                if (picked >= 0) m_selectedTile = picked;
            } else if (m_mode == EditLayer::Collision) {
                const int picked = CollisionPaletteHit(mouse, paletteOrigin);
                if (picked >= 0) m_selectedCol = picked;
            }
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
                // Mode-dependent values:
                //   Tile     → LMB paints m_selectedTile, RMB clears (-1)
                //   Collision→ LMB paints m_selectedCol, RMB clears (0 = Floor)
                const int leftVal  = (m_mode == EditLayer::Tile)
                                         ? m_selectedTile : m_selectedCol;
                const int rightVal = (m_mode == EditLayer::Tile) ? -1 : 0;
                if (Input::IsMouseDown(MouseButton::Left)) {
                    PaintCell(tx, ty, leftVal);
                } else if (Input::IsMouseDown(MouseButton::Right)) {
                    PaintCell(tx, ty, rightVal);
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
    enum class EditLayer : int { Tile = 0, Collision = 1 };
    struct StrokeEdit { EditLayer layer; int x, y, oldId, newId; };
    struct Stroke     { std::vector<StrokeEdit> edits; };
    static constexpr size_t kUndoLimit = 200;

    // Paint to the currently active layer. Routes to either the visual
    // Tilemap or the parallel collision grid based on m_mode.
    void PaintCell(int x, int y, int newId) {
        if (m_map.Width() <= 0) return;
        if (m_mode == EditLayer::Tile) {
            const int oldId = m_map.GetTile(x, y);
            if (oldId == newId) return;
            RecordEdit(EditLayer::Tile, x, y, oldId, newId);
            m_map.SetTile(x, y, newId);
        } else {
            EnsureCollisionGrid();
            const size_t idx = size_t(y) * m_map.Width() + size_t(x);
            const int oldId = int(m_collision[idx]);
            if (oldId == newId) return;
            RecordEdit(EditLayer::Collision, x, y, oldId, newId);
            m_collision[idx] = uint8_t(newId);
        }
        m_dirty = true;
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
        } else {
            EnsureCollisionGrid();
            m_collision[size_t(e.y) * m_map.Width() + size_t(e.x)] = uint8_t(v);
        }
    }

    void Undo() {
        if (m_undoStack.empty()) return;
        Stroke s = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        // Apply in reverse so overlapping edits inside one stroke wind back
        // to the original baseline rather than to an intermediate state.
        for (auto it = s.edits.rbegin(); it != s.edits.rend(); ++it) {
            ApplyEdit(*it, /*undo=*/true);
        }
        m_redoStack.push_back(std::move(s));
        m_dirty = true;
    }

    void Redo() {
        if (m_redoStack.empty()) return;
        Stroke s = std::move(m_redoStack.back());
        m_redoStack.pop_back();
        for (auto& e : s.edits) {
            ApplyEdit(e, /*undo=*/false);
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
                                                             : emptyCol);
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

        if (m_mode == EditLayer::Collision) {
            DrawCollisionPalette(r, origin);
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
        const char* modeName = (m_mode == EditLayer::Tile) ? "TILE"
                                                           : "COLLISION";
        const int   selVal   = (m_mode == EditLayer::Tile) ? m_selectedTile
                                                           : m_selectedCol;
        std::snprintf(buf, sizeof(buf),
                      "MAP: %dx%d   TILE: %dx%d   ZOOM: %.0f%%   "
                      "MODE: %s   SEL: %d   HOVER: %d,%d   "
                      "UNDO: %zu/%zu   %s   "
                      "LMB paint  RMB erase  MMB pan  WHEEL zoom  G grid  "
                      "L mode  CTRL+Z undo  CTRL+Y redo  CTRL+S save  "
                      "CTRL+N new  CTRL+O reload",
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
    std::vector<uint8_t> m_collision;
    EditLayer            m_mode        = EditLayer::Tile;
    int                  m_selectedCol = 1; // default Wall when entering mode
};

Application* NaraGame::CreateApplication(int argc, char** argv) {
    return new EditorApp(ParseArgs(argc, argv));
}
