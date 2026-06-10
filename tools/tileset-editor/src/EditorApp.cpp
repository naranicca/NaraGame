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

#include <NaraGame/assets/AssetWatcher.h>
#include <NaraGame/assets/MSDFFontLoader.h>
#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/assets/TilemapLoader.h>
#include <NaraGame/io/ConfigFile.h>
#include <NaraGame/renderer/MSDFFont.h>
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
// windows.h pulls in a DrawText macro (mapped to DrawTextW/A by the UNICODE
// switch) that collides with Renderer2D::DrawText. Drop it -- the editor
// doesn't use the Win32 GDI DrawText anywhere.
#  ifdef DrawText
#    undef DrawText
#  endif
#endif

using namespace NaraGame;

namespace {

constexpr float kScreenW   = 1280.0f;
constexpr float kScreenH   = 800.0f;
constexpr float kToolbarH  = 36.0f;
constexpr float kPaletteW  = 256.0f;
constexpr float kStatusH   = 24.0f;
// Tab strip at the top of the left palette so the user can click between
// Tile / Collision / Object modes without remembering Tab/L. Sits under
// the toolbar; the per-mode palette content begins below it.
constexpr float kTabStripH = 32.0f;

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

// Module-level handle to the MSDF font owned by EditorApp. Lets the free
// DrawMicroText/MeasureMicroText helpers below delegate to MSDF without
// threading a font pointer through every call site. Set in OnStart() once
// the font is loaded; left null while loading fails so the procedural
// 3x5 MicroFont path stays as a fallback.
static const MSDFFont* g_editorFont = nullptr;

// MSDF caps land roughly mid-em-box, so passing pixelSize through unchanged
// renders smaller-looking text than the old MicroFont (whose "pixelSize"
// was exactly the glyph height). Scale up so the new text occupies roughly
// the same vertical band the legacy 3x5 cells used to take.
constexpr float kMsdfScale = 1.35f;

// Draw small UI text. The editor calls this with `pos` as the top-left of
// the visible glyph box and `pixelSize` as the desired glyph height (sized
// for the legacy 3x5 MicroFont). MSDF path scales by kMsdfScale so the
// switchover doesn't shrink every label.
void DrawMicroText(Renderer2D& r, const std::string& text, Vec2 pos,
                   float pixelSize, const Color& color) {
    if (g_editorFont && g_editorFont->Valid()) {
        // Renderer2D::DrawText positions `pos.y` at the ascender line, not
        // at the visible cap top. For Inter (ascender 0.97em, cap height
        // ~0.72em) the cap top lands ~0.25 em below `pos.y`, which reads
        // as "text drifts low" compared to the legacy MicroFont where the
        // first pixel row sat exactly at `pos.y`. Pull pos.y up so the cap
        // top aligns with the original MicroFont top edge.
        const float effective = pixelSize * kMsdfScale;
        constexpr float kCapTopAdjust = 0.25f;
        Vec2 capPos = pos;
        capPos.y -= kCapTopAdjust * effective;
        r.DrawText(*g_editorFont, text, capPos, color, effective);
        return;
    }
    // Fallback: procedural 3x5 pixel font, used only if msdf-inter.json
    // failed to load. Identical behaviour to the original editor before
    // the font swap.
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
    if (g_editorFont && g_editorFont->Valid()) {
        return g_editorFont->Measure(text, pixelSize * kMsdfScale).x;
    }
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
        const std::string assets = NARAGAME_ASSETS_DIR;
        m_font = LoadMSDFFont(GetDevice(), assets + "/msdf-inter.json");
        if (m_font.Valid()) {
            g_editorFont = &m_font;
        } else {
            NARAGAME_WARN("editor: MSDF font load failed; falling back to "
                          "3x5 MicroFont.");
        }
        LoadRecentFiles();

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

        // Drain pending hot-reload events before anything else this frame
        // so the visible palette / canvas / minimap pick up the new
        // texture immediately. The callback only flips a flag; the
        // actual texture rebind runs here on the main thread where it's
        // safe to touch the GPU.
        m_tilesetWatcher.Poll();
        if (m_tilesetReloadPending) {
            m_tilesetReloadPending = false;
            ReloadTilesetTexture();
        }

        // Delete-symbol confirmation. Modal: Y / Enter delete, N / Esc
        // cancel. All other input is suppressed below so a stray click
        // doesn't dirty the map while the user is reading the count.
        if (m_deleteSymbolIdx >= 0) {
            const bool indexOk = size_t(m_deleteSymbolIdx) < m_symbols.size();
            if (!indexOk) { m_deleteSymbolIdx = -1; }
            else if (Input::IsKeyPressed(Key::Y) ||
                     Input::IsKeyPressed(Key::Enter)) {
                DeleteSymbolAt(m_deleteSymbolIdx);
                m_deleteSymbolIdx = -1;
            } else if (Input::IsKeyPressed(Key::N) ||
                       Input::IsKeyPressed(Key::Escape)) {
                m_deleteSymbolIdx = -1;
            }
            return;
        }

        // Add-symbol modal. Text input routes into:
        //   * m_addChar (until set) -- so the symbol is always the first
        //     character typed when the modal opens, no field-clicking
        //     needed for the common case.
        //   * Then either m_addLabel or m_addHex depending on m_addFocus,
        //     which the user switches by clicking on a field.
        // LMB on a colour swatch sets the tint and refreshes the hex
        // field to that swatch's value; LMB on a field switches focus.
        // Enter commits, Esc cancels.
        if (m_addSymbolModal) {
            for (char c : Input::TextInputThisFrame()) {
                if (c == 0x1B) { CloseAddSymbol(); return; }
                if (c == '\r' || c == '\n') {
                    CommitAddSymbol();
                    return;
                }
                if (c == '\b') {
                    // Backspace targets the focused field first, then
                    // peels back through (label or hex) -> char.
                    std::string& field = (m_addFocus == 1)
                        ? m_addHex : m_addLabel;
                    if (!field.empty()) field.pop_back();
                    else if (m_addChar != 0) m_addChar = 0;
                    if (m_addFocus == 1) TryParseHex();
                    continue;
                }
                if (c < 0x20) continue;
                if (m_addChar == 0) {
                    m_addChar = c;
                } else if (m_addFocus == 1) {
                    if (m_addHex.size() < 6 && IsHexDigit(c)) {
                        m_addHex.push_back(char(std::tolower(uint8_t(c))));
                        TryParseHex();
                    }
                } else if (m_addLabel.size() < 24) {
                    m_addLabel.push_back(c);
                }
            }
            if (Input::IsMousePressed(MouseButton::Left)) {
                const int sw = AddModalSwatchHit(mouse);
                if (sw >= 0) {
                    m_addColor = kObjAddSwatches[sw];
                    m_addHex   = ColorToHex(m_addColor);
                } else {
                    const int field = AddModalFieldHit(mouse);
                    if (field >= 0) m_addFocus = field;
                }
            }
            return;
        }

        // Marker metadata modal. Single text field for the marker's
        // free-form data string. Enter commits to the marker (and marks
        // the map dirty if the buffer changed), Esc cancels. If the
        // backing marker disappeared (e.g. undo), close silently.
        if (m_metaModalIdx >= 0) {
            if (size_t(m_metaModalIdx) >= m_objects.size()) {
                m_metaModalIdx = -1;
                m_metaModalBuf.clear();
                return;
            }
            for (char c : Input::TextInputThisFrame()) {
                if (c == 0x1B) {
                    m_metaModalIdx = -1;
                    m_metaModalBuf.clear();
                    return;
                }
                if (c == '\r' || c == '\n') {
                    auto& mref = m_objects[size_t(m_metaModalIdx)];
                    if (mref.data != m_metaModalBuf) {
                        mref.data = m_metaModalBuf;
                        m_dirty = true;
                    }
                    m_metaModalIdx = -1;
                    m_metaModalBuf.clear();
                    return;
                }
                if (c == '\b') {
                    if (!m_metaModalBuf.empty()) m_metaModalBuf.pop_back();
                    continue;
                }
                if (c < 0x20) continue;
                if (m_metaModalBuf.size() < 256) m_metaModalBuf.push_back(c);
            }
            return;
        }

        // Quit-confirm modal. Set when the user presses Esc with unsaved
        // changes -- gives them a chance to save before the editor exits.
        // S saves (popping a Save-As dialog if the map has no path yet),
        // D discards and quits, C / Esc cancels and returns to editing.
        if (m_quitConfirm) {
            if (Input::IsKeyPressed(Key::S)) {
                if (m_filePath.empty()) {
                    const std::string p = SaveFileDialog(
                        L"NaraGame tilemap (*.ntmap)\0*.ntmap\0All files\0*.*\0",
                        L"Save before quitting", L"ntmap");
                    if (p.empty()) return;       // user cancelled the dialog
                    m_filePath = p;
                }
                SaveCurrent();
                Close();
                return;
            }
            if (Input::IsKeyPressed(Key::D)) { Close(); return; }
            if (Input::IsKeyPressed(Key::C) ||
                Input::IsKeyPressed(Key::Escape)) {
                m_quitConfirm = false;
            }
            return;
        }

        // F1: toggle the keybind cheatsheet overlay. When it's up we treat
        // the editor as modal -- only F1/Esc clear it; paint / palette /
        // scrollbar are all suppressed so the user can read without
        // accidentally editing the map.
        if (Input::IsKeyPressed(Key::F1)) m_helpOverlay = !m_helpOverlay;
        if (m_helpOverlay) {
            if (Input::IsKeyPressed(Key::Escape)) m_helpOverlay = false;
            return;
        }

        // Recent-files overlay. Number keys pick an entry; Esc or
        // Ctrl+R again closes. Modal so a typo in the list doesn't
        // splat the canvas.
        if (m_recentOverlay) {
            if (Input::IsKeyPressed(Key::Escape)) {
                m_recentOverlay = false;
                return;
            }
            const Key digits[5] = {
                Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5,
            };
            for (int i = 0; i < kMaxRecent; ++i) {
                if (size_t(i) >= m_recent.size()) break;
                if (Input::IsKeyPressed(digits[i])) {
                    const std::string p = m_recent[size_t(i)];
                    m_recentOverlay = false;
                    OpenPath(p);
                    return;
                }
            }
            return;
        }

        // ----- Auto-backup timer -----
        // Runs only when there's something worth backing up: a path is
        // set (so the .bak target is well-defined) AND m_dirty is true.
        // Clearing m_dirty (after a successful Ctrl+S) resets the
        // timer in SaveCurrent(), so a "Ctrl+S then keep editing"
        // workflow gets the full kBackupInterval window before the
        // first auto-backup -- no surprise writes immediately after a
        // manual save.
        if (m_dirty && !m_filePath.empty()) {
            m_backupTimer += dt;
            if (m_backupTimer >= kBackupInterval) {
                m_backupTimer = 0.0f;
                WriteBackup();
            }
        } else {
            m_backupTimer = 0.0f;
        }

        // Esc: clear an active selection first, then prompt to save if the
        // map is dirty, then close. Lets the user back out of a marquee
        // without accidentally killing the editor, and prevents a one-tap
        // exit from throwing away unsaved work.
        if (Input::IsKeyPressed(Key::Escape)) {
            if (m_hasSelection || m_selDragging) {
                m_hasSelection = false;
                m_selDragging  = false;
            } else if (m_dirty) {
                m_quitConfirm = true;
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
        // H: toggle BOTH non-active overlays at once -- show-all <-> hide-all.
        // Shift+1 / Shift+2 toggle the collision tint and the object marker
        // overlays independently, so the user can isolate one layer without
        // hiding the other.
        if (Input::IsKeyPressed(Key::H)) {
            const bool any = m_showCollision || m_showObjects;
            m_showCollision = m_showObjects = !any;
        }
        const bool shiftHeld = Input::IsKeyDown(Key::LeftShift);
        if (shiftHeld && Input::IsKeyPressed(Key::Num1))
            m_showCollision = !m_showCollision;
        if (shiftHeld && Input::IsKeyPressed(Key::Num2))
            m_showObjects = !m_showObjects;
        // T: toggle auto-tile mode (Tile mode only). On enable, look
        // up the terrain containing m_selectedTile; if there is none
        // yet, register a fresh blob47 terrain starting at that tile.
        // Either way, the selected (or registered) terrain becomes
        // m_autoTileBase for the duration of the mode. While active
        // LMB / RMB use the canonical-mask paint path instead of the
        // stamp / brush. Off by default; terrains are persisted to
        // the .ntmap autotile section.
        if (Input::IsKeyPressed(Key::T) &&
            m_mode == EditLayer::Tile) {
            if (m_autoTile) {
                m_autoTile = false;
            } else if (m_selectedTile >= 0) {
                int base = -1;
                for (const auto& t : m_autotiles) {
                    if (m_selectedTile >= t.base &&
                        m_selectedTile <  t.base + t.count) {
                        base = t.base;
                        break;
                    }
                }
                if (base < 0) {
                    AutoTileTerrain nt;
                    nt.base  = m_selectedTile;
                    nt.count = kAutoTileCount;
                    nt.kind  = "blob47";
                    m_autotiles.push_back(nt);
                    base = nt.base;
                    m_dirty = true;
                }
                m_autoTile = true;
                m_autoTileBase = base;
            }
        }
        // [ / ]: brush size, photoshop-style. Sizes are odd so the brush
        // is centred on the hover cell. Object mode ignores it (markers
        // are always one per cell).
        if (Input::IsKeyPressed(Key::LeftBracket))  AdjustBrushSize(-1);
        if (Input::IsKeyPressed(Key::RightBracket)) AdjustBrushSize(+1);
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
        // Shift+digit is reserved for the layer-visibility toggles and
        // Ctrl+digit for the zoom presets; skip the palette quick-pick
        // when either modifier is held so a chord doesn't double-fire.
        if (m_map.Width() > 0 && !shiftHeld && !ctrl) {
            const Key digits[9] = {
                Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5,
                Key::Num6, Key::Num7, Key::Num8, Key::Num9,
            };
            for (int i = 0; i < 9; ++i) {
                if (!Input::IsKeyPressed(digits[i])) continue;
                if (m_mode == EditLayer::Tile) {
                    if (i < m_sheet.Count()) SetStampSingle(i);
                } else if (m_mode == EditLayer::Collision) {
                    if (i < 3) m_selectedCol = i;
                } else {
                    if (size_t(i) < m_symbols.size())
                        m_selectedSym = m_symbols[i].symbol;
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
            // Ctrl+Shift+S always pops the Save-As dialog regardless of
            // m_filePath -- it's the "save a copy / rename" path. Bare
            // Ctrl+S only pops the dialog on the first save and reuses
            // m_filePath thereafter.
            const bool shift = Input::IsKeyDown(Key::LeftShift);
            if (shift || m_filePath.empty()) {
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
        if (ctrl && Input::IsKeyPressed(Key::R)) {
            m_recentOverlay = !m_recentOverlay;
        }
        // Ctrl + 0/1/2/4: zoom presets. 0 = fit to canvas, others are
        // direct factors. Same anchor rule as wheel zoom (viewport
        // centre stays put) so the user keeps their bearings.
        if (ctrl && Input::IsKeyPressed(Key::Num0)) ZoomFit();
        if (ctrl && Input::IsKeyPressed(Key::Num1)) ZoomTo(1.0f);
        if (ctrl && Input::IsKeyPressed(Key::Num2)) ZoomTo(2.0f);
        if (ctrl && Input::IsKeyPressed(Key::Num4)) ZoomTo(4.0f);
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
        // Tab strip sits at the top of the panel; per-mode palette content
        // begins below it. The hit tests use the content rect so a click
        // on a tab never falls through to a palette entry.
        const Vec2 paletteContentOrigin{0.0f, kToolbarH + kTabStripH};
        const Vec2 paletteContentSize{kPaletteW,
            kScreenH - kToolbarH - kStatusH - kTabStripH};
        const int tabHit = TabIndexAt(mouse);
        if (tabHit >= 0 && Input::IsMousePressed(MouseButton::Left)) {
            const EditLayer next = static_cast<EditLayer>(tabHit);
            if (m_mode != next) {
                m_mode = next;
                if (m_mode == EditLayer::Collision) EnsureCollisionGrid();
            }
        }
        if (m_inPalette && wheel != 0.0f) {
            m_paletteScroll = std::max(0.0f,
                m_paletteScroll - wheel * float(m_tileH) * 2.0f);
        }
        if (m_inPalette && tabHit < 0 &&
            Input::IsMousePressed(MouseButton::Left)) {
            if (m_mode == EditLayer::Tile && m_currentTilesetPath.size()) {
                const int picked = PaletteHit(mouse,
                    paletteContentOrigin, paletteContentSize);
                if (picked >= 0) {
                    float cellPx; int cols, rows;
                    PaletteLayout(paletteContentOrigin, paletteContentSize,
                                  cellPx, cols, rows);
                    if (cols > 0) {
                        m_palDragging = true;
                        m_palAnchorX  = picked % cols;
                        m_palAnchorY  = picked / cols;
                        SetStampSingle(picked);
                    }
                }
            } else if (m_mode == EditLayer::Collision) {
                const int picked = CollisionPaletteHit(mouse,
                    paletteContentOrigin);
                if (picked >= 0) m_selectedCol = picked;
            } else if (m_mode == EditLayer::Object) {
                if (ObjectAddHit(mouse, paletteContentOrigin)) {
                    OpenAddSymbolModal();
                } else {
                    const int delIdx = ObjectDeleteHit(
                        mouse, paletteContentOrigin);
                    if (delIdx >= 0) {
                        m_deleteSymbolIdx = delIdx;
                    } else {
                        const int picked = ObjectPaletteHit(
                            mouse, paletteContentOrigin);
                        if (picked >= 0 &&
                            size_t(picked) < m_symbols.size())
                            m_selectedSym = m_symbols[picked].symbol;
                    }
                }
            }
        }
        // Palette LMB drag (Tile mode): extends the stamp to a rectangle
        // anchored at the press cell. Release ends the drag with whatever
        // rect was last under the cursor.
        if (m_palDragging) {
            if (!Input::IsMouseDown(MouseButton::Left)) {
                m_palDragging = false;
            } else if (m_mode == EditLayer::Tile) {
                const int cur = PaletteHit(mouse,
                    paletteContentOrigin, paletteContentSize);
                if (cur >= 0) {
                    float cellPx; int cols, rows;
                    PaletteLayout(paletteContentOrigin, paletteContentSize,
                                  cellPx, cols, rows);
                    if (cols > 0) {
                        const int cx = cur % cols;
                        const int cy = cur / cols;
                        if (cx != m_palAnchorX || cy != m_palAnchorY ||
                            m_stampW != 1 || m_stampH != 1) {
                            BuildStampFromPaletteRect(m_palAnchorX,
                                                      m_palAnchorY,
                                                      cx, cy, cols);
                        }
                    }
                }
            }
        }

        // ----- Scrollbar drag -----
        // Resolved before the paint block so latching the thumb suppresses
        // canvas painting for the duration of the drag. Anchor the scroll
        // bounds (hCamMin/Max etc.) and the camera->mouse scale at drag
        // start so the thumb tracks the cursor linearly through the drag,
        // even though the live ScrollGeo would shift as m_camera moves.
        // A click on the track (but NOT the thumb) page-jumps the camera
        // by one viewport in that direction.
        {
            const ScrollGeo geo = ComputeScrollGeo();
            if (m_scrollDrag == 0 &&
                Input::IsMousePressed(MouseButton::Left)) {
                const bool overThumbH = geo.hActive &&
                    mouse.x >= geo.hThumbX &&
                    mouse.x <  geo.hThumbX + geo.hThumbW &&
                    mouse.y >= geo.hTrack0.y &&
                    mouse.y <  geo.hTrack0.y + geo.hTrackSize.y;
                const bool overThumbV = geo.vActive &&
                    mouse.y >= geo.vThumbY &&
                    mouse.y <  geo.vThumbY + geo.vThumbH &&
                    mouse.x >= geo.vTrack0.x &&
                    mouse.x <  geo.vTrack0.x + geo.vTrackSize.x;
                if (overThumbH) {
                    m_scrollDrag         = 1;
                    m_scrollAnchorMouse  = mouse.x;
                    m_scrollAnchorCam    = m_camera.x;
                    m_scrollAnchorCamMin = geo.hCamMin;
                    m_scrollAnchorCamMax = geo.hCamMax;
                    m_scrollAnchorScale  =
                        (geo.hCamMax - geo.hCamMin) /
                        std::max(1.0f, geo.hTrackRange);
                } else if (overThumbV) {
                    m_scrollDrag         = 2;
                    m_scrollAnchorMouse  = mouse.y;
                    m_scrollAnchorCam    = m_camera.y;
                    m_scrollAnchorCamMin = geo.vCamMin;
                    m_scrollAnchorCamMax = geo.vCamMax;
                    m_scrollAnchorScale  =
                        (geo.vCamMax - geo.vCamMin) /
                        std::max(1.0f, geo.vTrackRange);
                } else if (geo.hActive &&
                           IsInside(mouse, geo.hTrack0, geo.hTrackSize)) {
                    // Page-jump horizontal: ±1 viewport.
                    const float viewW = (kScreenW - kPaletteW) / m_zoom;
                    const float dir = (mouse.x < geo.hThumbX)
                                        ? -1.0f : 1.0f;
                    m_camera.x = std::clamp(m_camera.x + dir * viewW,
                                            geo.hCamMin, geo.hCamMax);
                } else if (geo.vActive &&
                           IsInside(mouse, geo.vTrack0, geo.vTrackSize)) {
                    const float viewH =
                        (kScreenH - kToolbarH - kStatusH) / m_zoom;
                    const float dir = (mouse.y < geo.vThumbY)
                                        ? -1.0f : 1.0f;
                    m_camera.y = std::clamp(m_camera.y + dir * viewH,
                                            geo.vCamMin, geo.vCamMax);
                }
            }
            if (m_scrollDrag != 0) {
                if (!Input::IsMouseDown(MouseButton::Left)) {
                    m_scrollDrag = 0;
                } else {
                    const float axisMouse =
                        (m_scrollDrag == 1) ? mouse.x : mouse.y;
                    const float target = std::clamp(
                        m_scrollAnchorCam +
                            (axisMouse - m_scrollAnchorMouse) *
                                m_scrollAnchorScale,
                        m_scrollAnchorCamMin, m_scrollAnchorCamMax);
                    if (m_scrollDrag == 1) m_camera.x = target;
                    else                   m_camera.y = target;
                }
            }
        }

        // ----- Minimap click / drag -----
        // LMB inside the minimap centres the camera on the clicked world
        // position. Holding drag lets the user track-pad around the map
        // while keeping the cursor inside the panel. Sets m_minimapDrag
        // so the canvas paint / edge resize / scrollbar blocks below all
        // skip this frame -- otherwise a click on the corner of the
        // minimap would also paint into a stray bottom-right tile.
        {
            const MinimapGeo mg = ComputeMinimap();
            const bool overMm = mg.active &&
                IsInside(mouse, mg.origin, mg.size);
            if (!m_minimapDrag && overMm &&
                Input::IsMousePressed(MouseButton::Left)) {
                m_minimapDrag = true;
            }
            if (m_minimapDrag) {
                if (!Input::IsMouseDown(MouseButton::Left) || !mg.active) {
                    m_minimapDrag = false;
                } else {
                    const float fx = std::clamp(
                        (mouse.x - mg.origin.x) / mg.size.x, 0.0f, 1.0f);
                    const float fy = std::clamp(
                        (mouse.y - mg.origin.y) / mg.size.y, 0.0f, 1.0f);
                    const float worldX = fx *
                        float(m_map.Width()  * m_tileW);
                    const float worldY = fy *
                        float(m_map.Height() * m_tileH);
                    const float viewW = (kScreenW - kPaletteW) / m_zoom;
                    const float viewH = (kScreenH - kToolbarH - kStatusH)
                                       / m_zoom;
                    m_camera.x = worldX - viewW * 0.5f;
                    m_camera.y = worldY - viewH * 0.5f;
                }
            }
            if (overMm && !m_minimapDrag) {
                // Suppress the canvas paint hover that sits behind so a
                // hover over the minimap doesn't keep painting an
                // outline on the tile underneath it.
                m_hoverTx = -1;
                m_hoverTy = -1;
            }
        }

        // ----- Map edge resize drag -----
        // Hover within a few pixels of a visible map edge -> highlight it
        // and capture LMB press to drag that edge in/out. Suppresses the
        // canvas paint block below for the duration so a missed grab
        // doesn't paint a stripe along the boundary.
        if (m_scrollDrag == 0 && !ctrl && !m_minimapDrag &&
            m_map.Width() > 0) {
            if (!m_resizingMap) {
                const ResizeEdge edge = HoveredResizeEdge(
                    mouse, canvasOrigin, canvasSize);
                m_resizeHoverEdge = edge;
                if (edge != ResizeEdge::None &&
                    Input::IsMousePressed(MouseButton::Left)) {
                    m_resizingMap       = true;
                    m_resizingEdge      = edge;
                    m_resizeMouseOrigin = mouse;
                }
            } else {
                if (!Input::IsMouseDown(MouseButton::Left)) {
                    const int delta = ComputeResizeDelta(
                        m_resizingEdge, mouse);
                    if (delta != 0) DoResize(m_resizingEdge, delta);
                    m_resizingMap     = false;
                    m_resizeHoverEdge = ResizeEdge::None;
                }
            }
        } else {
            m_resizeHoverEdge = ResizeEdge::None;
        }

        // ----- Canvas paint / erase / fill / rect -----
        if (m_inCanvas && m_map.Width() > 0 && !m_panning &&
            m_scrollDrag == 0 && !m_resizingMap && !m_minimapDrag) {
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
                        if (id >= 0) SetStampSingle(id);
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
                        // LMB rect in Tile mode tiles the stamp across the
                        // rectangle (so a 2x2 stamp paints a clean checker
                        // pattern). RMB rect, and non-Tile modes, fall
                        // back to single-value fill.
                        const bool stampTile =
                            (m_rectLastButton == 0 &&
                             m_mode == EditLayer::Tile &&
                             !m_stampTiles.empty());
                        const int v = (m_rectLastButton == 0)
                                          ? leftVal : rightVal;
                        for (int yy = y0; yy <= y1; ++yy) {
                            for (int xx = x0; xx <= x1; ++xx) {
                                if (stampTile) {
                                    const int sx = (xx - x0) % m_stampW;
                                    const int sy = (yy - y0) % m_stampH;
                                    const int sv = m_stampTiles[
                                        size_t(sy) * m_stampW + sx];
                                    if (sv >= 0) PaintCell(xx, yy, sv);
                                } else {
                                    PaintCell(xx, yy, v);
                                }
                            }
                        }
                        EndStroke();
                        m_rectActive = false;
                    }
                } else if (inMap) {
                    if (m_mode == EditLayer::Object) {
                        // Press-on-marker enters a drag candidate state;
                        // release decides between move (cursor wandered
                        // off source) and replace (didn't move). Press
                        // on an empty cell still places immediately and
                        // continues to spray on hold like the other
                        // modes, so the existing "drag a row of enemies"
                        // workflow is preserved.
                        if (Input::IsMousePressed(MouseButton::Left)) {
                            const int existing = GetObjectSymbol(tx, ty);
                            if (existing != 0) {
                                m_objDragSym   = char(existing);
                                m_objDragSrcTx = tx;
                                m_objDragSrcTy = ty;
                                m_objDragging  = false;
                            } else {
                                PaintCell(tx, ty, leftVal);
                            }
                        }
                        if (m_objDragSym != 0 &&
                            Input::IsMouseDown(MouseButton::Left)) {
                            if (tx != m_objDragSrcTx ||
                                ty != m_objDragSrcTy) m_objDragging = true;
                        } else if (m_objDragSym == 0 &&
                                   Input::IsMouseDown(MouseButton::Left)) {
                            PaintCell(tx, ty, leftVal);
                        }
                        // RMB: press on an existing marker opens the
                        // metadata edit modal (don't erase). Press on an
                        // empty cell, or dragging RMB across cells,
                        // erases as before so the legacy "drag a row of
                        // erases" workflow still works.
                        if (Input::IsMousePressed(MouseButton::Right)) {
                            const int idx = FindObjectIndex(tx, ty);
                            if (idx >= 0) {
                                m_metaModalIdx = idx;
                                m_metaModalBuf =
                                    m_objects[size_t(idx)].data;
                            } else {
                                PaintCell(tx, ty, rightVal);
                            }
                        } else if (Input::IsMouseDown(MouseButton::Right)) {
                            PaintCell(tx, ty, rightVal);
                        }
                    } else if (m_mode == EditLayer::Tile && m_autoTile) {
                        if (Input::IsMouseDown(MouseButton::Left)) {
                            AutoTilePaintAt(tx, ty);
                        } else if (Input::IsMouseDown(MouseButton::Right)) {
                            AutoTileEraseAt(tx, ty);
                        }
                    } else {
                        if (Input::IsMouseDown(MouseButton::Left)) {
                            PaintBrushAt(tx, ty, leftVal,
                                         /*isLeft=*/true);
                        } else if (Input::IsMouseDown(MouseButton::Right)) {
                            PaintBrushAt(tx, ty, rightVal,
                                         /*isLeft=*/false);
                        }
                    }
                }
            }
        } else {
            m_hoverTx = -1;
            m_hoverTy = -1;
        }

        // Resolve any pending marker drag on LMB release. Lives outside
        // the canvas-paint guard so a release after the cursor strays
        // off the canvas still cleans up the drag state.
        if (m_objDragSym != 0 &&
            !Input::IsMouseDown(MouseButton::Left)) {
            const bool destValid =
                m_hoverTx >= 0 && m_hoverTx < m_map.Width() &&
                m_hoverTy >= 0 && m_hoverTy < m_map.Height();
            if (!m_objDragging) {
                // No drag at all: treat the press as a click-to-replace
                // (legacy behaviour) -- swap the source marker for the
                // currently selected symbol.
                PaintCell(m_objDragSrcTx, m_objDragSrcTy,
                          int(uint8_t(m_selectedSym)));
            } else if (destValid &&
                       (m_hoverTx != m_objDragSrcTx ||
                        m_hoverTy != m_objDragSrcTy)) {
                // Drag with a valid different destination: move.
                PaintCell(m_objDragSrcTx, m_objDragSrcTy, 0);
                PaintCell(m_hoverTx, m_hoverTy,
                          int(uint8_t(m_objDragSym)));
            }
            // else (drag returned to source / dest invalid): cancel quietly.
            m_objDragSym  = 0;
            m_objDragging = false;
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
        if (m_helpOverlay) DrawHelpOverlay(r);
        if (m_quitConfirm) DrawQuitConfirm(r);
        if (m_deleteSymbolIdx >= 0) DrawDeleteSymbolModal(r);
        if (m_addSymbolModal) DrawAddSymbolModal(r);
        if (m_metaModalIdx >= 0) DrawMetadataModal(r);
        if (m_recentOverlay) DrawRecentOverlay(r);

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

    // Stamp helpers. m_selectedTile mirrors the stamp's top-left palette
    // index so existing call sites that look at "the selected tile" stay
    // correct for the common 1x1 case.
    void SetStampSingle(int paletteIdx) {
        m_selectedTile = paletteIdx;
        m_stampW = 1;
        m_stampH = 1;
        m_stampTiles = {paletteIdx};
    }
    void BuildStampFromPaletteRect(int ax, int ay, int bx, int by,
                                   int palCols) {
        if (palCols <= 0) { SetStampSingle(m_selectedTile); return; }
        if (ax > bx) std::swap(ax, bx);
        if (ay > by) std::swap(ay, by);
        m_stampW = bx - ax + 1;
        m_stampH = by - ay + 1;
        m_stampTiles.clear();
        m_stampTiles.reserve(size_t(m_stampW) * size_t(m_stampH));
        const int total = m_sheet.Count();
        for (int y = ay; y <= by; ++y) {
            for (int x = ax; x <= bx; ++x) {
                const int idx = y * palCols + x;
                m_stampTiles.push_back(idx < total ? idx : -1);
            }
        }
        m_selectedTile = ay * palCols + ax;
    }
    // Paint the stamp at canvas (tx, ty) — top-left aligned. Cells with
    // a sentinel -1 (palette rect overhanging the tileset end) are
    // skipped so the user doesn't unintentionally erase tiles.
    // Brush size step. We keep odd sizes so the brush footprint is
    // perfectly centred on the hover cell (radius = (n-1)/2). Object
    // mode collapses back to 1 -- placing N copies of a marker on the
    // same cell can't happen anyway (one per cell), so honouring the
    // brush there would be misleading.
    void AdjustBrushSize(int dir) {
        static constexpr int kBrushSizes[] = {1, 3, 5, 7, 9};
        static constexpr int kCount =
            int(sizeof(kBrushSizes) / sizeof(kBrushSizes[0]));
        int idx = 0;
        for (int i = 0; i < kCount; ++i)
            if (kBrushSizes[i] == m_brushSize) { idx = i; break; }
        idx = std::clamp(idx + dir, 0, kCount - 1);
        m_brushSize = kBrushSizes[idx];
    }

    int EffectiveBrushSize() const {
        return (m_mode == EditLayer::Object) ? 1 : m_brushSize;
    }

    // Top-level paint dispatch from the click-and-drag handler. For a
    // 1x1 brush we route Tile mode through PaintStampAt so multi-cell
    // stamps still work; everything else paints a single value into the
    // brush footprint. Stamps and brush size are intentionally
    // exclusive -- mixing them would require deciding whether the stamp
    // tiles inside the brush footprint or strides across it, and the
    // current users haven't asked for either.
    void PaintBrushAt(int tx, int ty, int val, bool isLeft) {
        if (m_map.Width() <= 0) return;
        const int n = EffectiveBrushSize();
        if (n <= 1 && m_mode == EditLayer::Tile && isLeft) {
            PaintStampAt(tx, ty);
            return;
        }
        const int r = (n - 1) / 2;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int xx = tx + dx;
                const int yy = ty + dy;
                if (xx < 0 || yy < 0 ||
                    xx >= m_map.Width() || yy >= m_map.Height()) continue;
                PaintCell(xx, yy, val);
            }
        }
    }

    // True if (x, y) is off the map OR holds a tile in the current
    // terrain block. Off-map = "same" makes border cells use the
    // surrounded-variant index so an autotiled mass meets the map
    // edge cleanly instead of fraying.
    bool AutoTileIsSame(int x, int y) const {
        if (x < 0 || y < 0 ||
            x >= m_map.Width() || y >= m_map.Height()) return true;
        const int id = m_map.GetTile(x, y);
        return id >= m_autoTileBase &&
               id <  m_autoTileBase + kAutoTileCount;
    }

    // Build the canonical 8-bit mask for (x, y). Corner bits are
    // automatically suppressed when either of their adjacent cardinals
    // is not "same", so a corner only counts in for an inside-corner
    // (both adjacent sides are also terrain).
    int AutoTileMaskAt(int x, int y) const {
        int m = 0;
        if (AutoTileIsSame(x,     y - 1)) m |= 0x01;  // N
        if (AutoTileIsSame(x + 1, y - 1)) m |= 0x02;  // NE
        if (AutoTileIsSame(x + 1, y))     m |= 0x04;  // E
        if (AutoTileIsSame(x + 1, y + 1)) m |= 0x08;  // SE
        if (AutoTileIsSame(x,     y + 1)) m |= 0x10;  // S
        if (AutoTileIsSame(x - 1, y + 1)) m |= 0x20;  // SW
        if (AutoTileIsSame(x - 1, y))     m |= 0x40;  // W
        if (AutoTileIsSame(x - 1, y - 1)) m |= 0x80;  // NW
        const bool n = (m & 0x01) != 0;
        const bool e = (m & 0x04) != 0;
        const bool s = (m & 0x10) != 0;
        const bool w = (m & 0x40) != 0;
        if (!(n && e)) m &= ~0x02;
        if (!(s && e)) m &= ~0x08;
        if (!(s && w)) m &= ~0x20;
        if (!(n && w)) m &= ~0x80;
        return m;
    }

    // Refresh the variant at (x, y) by recomputing its mask. forcePlace
    // overrides the "already-belongs-to-terrain" gate so the centre
    // cell can be converted into the terrain on first paint without
    // the surrounding cells getting promoted along with it.
    void AutoTileRefreshCell(int x, int y, bool forcePlace) {
        if (x < 0 || y < 0 ||
            x >= m_map.Width() || y >= m_map.Height()) return;
        const int existing = m_map.GetTile(x, y);
        const bool belongs = existing >= m_autoTileBase &&
                             existing <  m_autoTileBase + kAutoTileCount;
        if (!forcePlace && !belongs) return;
        const int variant = AutoMaskToVariantTable()[AutoTileMaskAt(x, y)];
        if (variant < 0) return;  // shouldn't happen for canonical masks
        const int newId = m_autoTileBase + variant;
        if (existing != newId) PaintCell(x, y, newId);
    }

    void AutoTilePaintAt(int tx, int ty) {
        AutoTileRefreshCell(tx, ty, /*forcePlace=*/true);
        // 8-neighbour refresh because corner bits can flip on diagonals
        // too -- a 4-neighbour pass would leave the diagonally-touching
        // cells with stale variants.
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy)
                    AutoTileRefreshCell(tx + dx, ty + dy,
                                        /*forcePlace=*/false);
    }

    void AutoTileEraseAt(int tx, int ty) {
        if (m_map.GetTile(tx, ty) != -1) PaintCell(tx, ty, -1);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy)
                    AutoTileRefreshCell(tx + dx, ty + dy,
                                        /*forcePlace=*/false);
    }

    void PaintStampAt(int tx, int ty) {
        if (m_stampTiles.empty()) return;
        for (int sy = 0; sy < m_stampH; ++sy) {
            for (int sx = 0; sx < m_stampW; ++sx) {
                const int v = m_stampTiles[size_t(sy) * m_stampW + sx];
                if (v < 0) continue;
                PaintCell(tx + sx, ty + sy, v);
            }
        }
    }

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
    // Returns the m_objects index of the marker at (x,y), or -1 if none.
    // Used by the RMB metadata modal trigger.
    int FindObjectIndex(int x, int y) const {
        for (size_t i = 0; i < m_objects.size(); ++i)
            if (m_objects[i].x == x && m_objects[i].y == y) return int(i);
        return -1;
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
    enum class ResizeEdge { None, Left, Right, Top, Bottom };

    // Pixel slop around an edge that counts as "hovering" it. Wide enough
    // to give a comfortable grab area, narrow enough to never collide
    // with paint clicks one cell in from the edge.
    static constexpr float kResizeHandleSlop = 6.0f;

    // Which edge -- if any -- the cursor is within kResizeHandleSlop px
    // of. Returns None when nothing's hovered. Corners deliberately pick
    // the horizontal edge so the user doesn't accidentally fall into a
    // two-axis drag (which DoResize can't do in one call anyway).
    ResizeEdge HoveredResizeEdge(const Vec2& mouse,
                                 const Vec2& canvasOrigin,
                                 const Vec2& canvasSize) const {
        if (m_map.Width() <= 0) return ResizeEdge::None;
        if (!IsInside(mouse, canvasOrigin, canvasSize))
            return ResizeEdge::None;
        const Vec2 tl = WorldToScreen({0.0f, 0.0f}, canvasOrigin);
        const Vec2 br = WorldToScreen(
            {float(m_map.Width()  * m_tileW),
             float(m_map.Height() * m_tileH)}, canvasOrigin);
        const float s = kResizeHandleSlop;
        const bool yInBand = (mouse.y >= tl.y - s && mouse.y <= br.y + s);
        const bool xInBand = (mouse.x >= tl.x - s && mouse.x <= br.x + s);
        // Vertical edges (Left/Right) take priority over Top/Bottom for
        // the corner case so a click at the top-left targets a single
        // axis predictably.
        if (yInBand && std::abs(mouse.x - tl.x) <= s) return ResizeEdge::Left;
        if (yInBand && std::abs(mouse.x - br.x) <= s) return ResizeEdge::Right;
        if (xInBand && std::abs(mouse.y - tl.y) <= s) return ResizeEdge::Top;
        if (xInBand && std::abs(mouse.y - br.y) <= s) return ResizeEdge::Bottom;
        return ResizeEdge::None;
    }

    // Mouse delta in pixels since drag start -> delta rows/cols on the
    // dragged edge. Sign convention follows DoResize: positive delta
    // *grows* the map on that edge (Left grow extends to the left,
    // Right grow extends to the right). Clamped so the map stays >= 1
    // tile on each axis.
    int ComputeResizeDelta(ResizeEdge edge, const Vec2& mouse) const {
        const float dx = mouse.x - m_resizeMouseOrigin.x;
        const float dy = mouse.y - m_resizeMouseOrigin.y;
        const float pxPerCellW = float(m_tileW) * m_zoom;
        const float pxPerCellH = float(m_tileH) * m_zoom;
        int delta = 0;
        switch (edge) {
            case ResizeEdge::Right:
                delta =  int(std::round(dx / pxPerCellW)); break;
            case ResizeEdge::Left:
                delta = -int(std::round(dx / pxPerCellW)); break;
            case ResizeEdge::Bottom:
                delta =  int(std::round(dy / pxPerCellH)); break;
            case ResizeEdge::Top:
                delta = -int(std::round(dy / pxPerCellH)); break;
            case ResizeEdge::None: break;
        }
        const int currentDim = (edge == ResizeEdge::Left ||
                                edge == ResizeEdge::Right)
                                   ? m_map.Width() : m_map.Height();
        if (currentDim + delta < 1) delta = 1 - currentDim;
        return delta;
    }

    void DoResize(ResizeEdge edge, int delta) {
        if (m_map.Width() <= 0 || delta == 0) return;
        if (edge == ResizeEdge::None) return;
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

    // Resolve %APPDATA%/NaraGame/editor-recent.cfg. Falls back to a
    // relative path next to the editor binary if APPDATA isn't set
    // (shouldn't happen on stock Windows, but the fall-through keeps
    // the feature from silently losing the list).
    static std::string RecentConfigPath() {
#if defined(_WIN32)
        wchar_t buf[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf,
                                                MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            // Wide → UTF-8.
            const int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1,
                nullptr, 0, nullptr, nullptr);
            std::string base(len > 0 ? size_t(len) - 1 : 0, '\0');
            if (len > 0)
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, base.data(),
                                    len, nullptr, nullptr);
            // CreateDirectoryW ignores ALREADY_EXISTS so the call is
            // safe whether the folder is new or not.
            const std::wstring naraDir = std::wstring(buf) +
                                         L"\\NaraGame";
            CreateDirectoryW(naraDir.c_str(), nullptr);
            return base + "\\NaraGame\\editor-recent.cfg";
        }
#endif
        return "editor-recent.cfg";
    }

    void LoadRecentFiles() {
        ConfigFile cfg;
        cfg.Load(RecentConfigPath());
        m_recent.clear();
        for (int i = 0; i < kMaxRecent; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), "recent%d", i);
            const std::string v = cfg.GetString(key, "");
            if (!v.empty()) m_recent.push_back(v);
        }
    }

    void SaveRecentFiles() const {
        ConfigFile cfg;
        for (int i = 0; i < int(m_recent.size()) && i < kMaxRecent; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), "recent%d", i);
            cfg.SetString(key, m_recent[size_t(i)]);
        }
        cfg.Save(RecentConfigPath());
    }

    // Push to most-recent; dedup case-insensitively (Windows paths) and
    // trim to kMaxRecent. Saved immediately so an editor crash mid-edit
    // doesn't lose the list.
    void PushRecent(const std::string& path) {
        if (path.empty()) return;
        auto sameFile = [&](const std::string& a) {
            if (a.size() != path.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                const char ca = char(std::tolower(uint8_t(a[i])));
                const char cb = char(std::tolower(uint8_t(path[i])));
                if (ca != cb) return false;
            }
            return true;
        };
        m_recent.erase(
            std::remove_if(m_recent.begin(), m_recent.end(), sameFile),
            m_recent.end());
        m_recent.insert(m_recent.begin(), path);
        if (int(m_recent.size()) > kMaxRecent)
            m_recent.resize(kMaxRecent);
        SaveRecentFiles();
    }

    static std::string DirComponent(const std::string& path) {
        const size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos) return {};
        return path.substr(0, slash + 1);
    }
    static std::string LeafComponent(const std::string& path) {
        const size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos) return path;
        return path.substr(slash + 1);
    }
    static std::string ResolveAgainst(const std::string& baseFile,
                                      const std::string& rel) {
        if (rel.size() >= 2 && rel[1] == ':') return rel;          // C:\...
        if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) return rel;
        return DirComponent(baseFile) + rel;
    }

    // Install / replace the watcher on the directory containing the
    // current tileset. The leaf is captured by value so the callback
    // closure stays valid through a later Watch() that re-replaces it.
    // Set m_zoom to newZoom while keeping the world point currently
    // under the canvas centre at the canvas centre. Mirrors the
    // wheel-zoom-at-cursor logic but uses the viewport midpoint as the
    // anchor so a "Ctrl+2 / 200%" keystroke doesn't yank the user to
    // some random spot.
    void ZoomTo(float newZoom) {
        if (newZoom <= 0.0f) return;
        const Vec2 canvasOrigin{kPaletteW, kToolbarH};
        const Vec2 canvasCentre{
            kPaletteW + (kScreenW - kPaletteW) * 0.5f,
            kToolbarH + (kScreenH - kToolbarH - kStatusH) * 0.5f};
        const Vec2 worldBefore =
            ScreenToWorld(canvasCentre, canvasOrigin, m_zoom);
        m_zoom = newZoom;
        const Vec2 worldAfter =
            ScreenToWorld(canvasCentre, canvasOrigin, m_zoom);
        m_camera.x += worldBefore.x - worldAfter.x;
        m_camera.y += worldBefore.y - worldAfter.y;
    }

    // Fit the whole map inside the canvas viewport: pick the largest
    // zoom that still shows everything, then centre the map on the
    // canvas. Useful for jumping back to overview after working in a
    // detail zoom level.
    void ZoomFit() {
        if (m_map.Width() <= 0) return;
        const float canvasW = kScreenW - kPaletteW;
        const float canvasH = kScreenH - kToolbarH - kStatusH;
        const float mapW = float(m_map.Width() * m_tileW);
        const float mapH = float(m_map.Height() * m_tileH);
        if (mapW <= 0.0f || mapH <= 0.0f) return;
        m_zoom = std::min(canvasW / mapW, canvasH / mapH);
        // Centre: camera = mapCentre - viewportCentre / zoom
        m_camera.x = mapW * 0.5f - canvasW * 0.5f / m_zoom;
        m_camera.y = mapH * 0.5f - canvasH * 0.5f / m_zoom;
    }

    void StartTilesetWatcher() {
        if (m_currentTilesetAbsPath.empty()) return;
        const std::string dir  = DirComponent(m_currentTilesetAbsPath);
        const std::string leaf = LeafComponent(m_currentTilesetAbsPath);
        if (dir.empty() || leaf.empty()) return;
        // AssetWatcher emits paths relative to the watched root using
        // forward slashes; the leaf has no separators so a direct
        // compare is enough.
        m_tilesetWatcher.Watch(dir,
            [this, leaf](const std::string& rel) {
                if (rel == leaf) m_tilesetReloadPending = true;
            });
    }

    // Re-LoadTexture and rebind the SpriteSheet. The Tilemap holds a
    // raw pointer through its SpriteSheet, so the rebind has to happen
    // after the new texture is owned by m_tilesetTex.
    void ReloadTilesetTexture() {
        if (m_currentTilesetAbsPath.empty()) return;
        std::unique_ptr<RHITexture> fresh = LoadTexture(
            GetDevice(), m_currentTilesetAbsPath, TextureFilter::Point);
        if (!fresh) {
            NARAGAME_WARN("editor: tileset hot-reload failed for '%s'",
                          m_currentTilesetAbsPath.c_str());
            return;
        }
        m_tilesetTex = std::move(fresh);
        m_sheet = SpriteSheet(m_tilesetTex.get(),
                              m_tileW, m_tileH, 0, 0);
        m_map.SetTileset(m_sheet);
        NARAGAME_INFO("editor: tileset hot-reloaded '%s'",
                      m_currentTilesetAbsPath.c_str());
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
        // A map that authored its own `symbols` section wins over the editor
        // default. Maps with no section fall back to the default fill so the
        // user can still place markers when editing legacy files.
        m_symbols    = !asset.symbols.empty() ? std::move(asset.symbols)
                                              : DefaultObjectSymbols();
        m_autotiles  = std::move(asset.autotiles);  // empty if absent
        m_autoTile   = false;
        // The Tilemap stored a SpriteSheet pointing at the asset's texture
        // before we moved it. Rebind so it points at our owned copy.
        m_tileW = asset.tileWidth;
        m_tileH = asset.tileHeight;
        SpriteSheet sheet(m_tilesetTex.get(), m_tileW, m_tileH, 0, 0);
        m_map.SetTileset(sheet);
        m_sheet = sheet;
        m_currentTilesetPath = asset.tilesetPath;
        m_currentTilesetAbsPath = ResolveAgainst(path, asset.tilesetPath);
        m_filePath  = path;
        m_camera    = {0.0f, 0.0f};
        m_zoom      = 1.0f;
        m_paletteScroll = 0.0f;
        SetStampSingle(0);
        ClearHistory();
        StartTilesetWatcher();
        PushRecent(path);
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
        m_symbols = DefaultObjectSymbols();
        m_autotiles.clear();
        m_autoTile = false;
        m_currentTilesetPath = tilesetPath;
        m_currentTilesetAbsPath = tilesetPath;
        m_camera = {0.0f, 0.0f};
        m_zoom   = 1.0f;
        m_paletteScroll = 0.0f;
        SetStampSingle(0);
        ClearHistory();
        StartTilesetWatcher();
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
                                  m_objects, m_symbols, m_autotiles);
        if (ok) {
            m_filePath = out;
            m_dirty = false;
            // Explicit save invalidates the in-flight backup window --
            // the user's "real" copy on disk is now current.
            m_backupTimer = 0.0f;
            NARAGAME_INFO("editor: saved '%s'", out.c_str());
        }
    }

    // Sibling .bak write, fired by the periodic timer in OnUpdate while
    // m_dirty is true. Uses the same SaveNtmap path as Ctrl+S but writes
    // to "<m_filePath>.bak" and leaves m_dirty alone -- the user's
    // working state is untouched.
    void WriteBackup() {
        if (m_filePath.empty() || m_map.Width() <= 0) return;
        const std::string bak = m_filePath + ".bak";
        const size_t need = size_t(m_map.Width()) *
                            size_t(m_map.Height());
        const std::vector<uint8_t> emptyCol;
        const bool ok = SaveNtmap(bak, m_map, m_currentTilesetPath,
                                  m_tileW, m_tileH, 0, 0,
                                  m_collision.size() == need ? m_collision
                                                             : emptyCol,
                                  m_objects, m_symbols, m_autotiles);
        if (ok)
            NARAGAME_INFO("editor: backup '%s'", bak.c_str());
        else
            NARAGAME_WARN("editor: backup failed for '%s'", bak.c_str());
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

    // Minimap layout shared by DrawCanvasPanel (renders the panel + tiles
    // + viewport box) and OnUpdate (LMB press/drag jumps the camera).
    // Sits in the canvas's top-right corner; cell size adapts to the map
    // aspect so a wide map (mario level1) reads as a short strip and a
    // square map (brawl arena) fills out the panel. Inactive when the
    // map is small enough that the minimap would just duplicate the
    // canvas at the same scale.
    struct MinimapGeo {
        bool  active = false;
        Vec2  origin{};
        Vec2  size{};
        float cellSize = 0.0f;
    };

    MinimapGeo ComputeMinimap() const {
        MinimapGeo g;
        if (m_map.Width() <= 0) return g;
        constexpr float kMargin = 12.0f;
        constexpr float kMaxW   = 240.0f;
        constexpr float kMaxH   = 180.0f;
        const float maxW = kMaxW;
        const float maxH = kMaxH;
        const float byW = maxW / float(m_map.Width());
        const float byH = maxH / float(m_map.Height());
        // Floor to whole pixels so tiles align cleanly; cap at 6 px so a
        // tiny map doesn't blow the panel up larger than necessary.
        float cell = std::min(byW, byH);
        if (cell > 6.0f) cell = 6.0f;
        if (cell < 1.0f) cell = 1.0f;
        cell = std::floor(cell);
        const float mmW = cell * float(m_map.Width());
        const float mmH = cell * float(m_map.Height());
        // Hide when the canvas already shows the whole map comfortably
        // (avoids visual clutter on tiny maps + zoom out).
        const float canvasW = kScreenW - kPaletteW;
        const float canvasH = kScreenH - kToolbarH - kStatusH;
        const float visibleW = float(m_map.Width()  * m_tileW) * m_zoom;
        const float visibleH = float(m_map.Height() * m_tileH) * m_zoom;
        if (visibleW <= canvasW && visibleH <= canvasH) return g;
        g.active   = true;
        g.cellSize = cell;
        g.size     = {mmW, mmH};
        g.origin   = {kPaletteW + canvasW - mmW - kMargin,
                      kToolbarH + kMargin};
        return g;
    }

    // Scrollbar layout shared by DrawCanvasPanel (renders the bars) and
    // OnUpdate (drives the drag interaction). Scroll extent is the bbox of
    // {map rect, current viewport rect} in world units, so the bar appears
    // whenever there is map outside the canvas -- including the case where
    // the map is smaller than the canvas but the camera has shifted it
    // off-screen. {h,v}Cam{Min,Max} bracket the camera positions that keep
    // the thumb fully on the track.
    struct ScrollGeo {
        bool  hActive = false, vActive = false;
        Vec2  hTrack0{}, hTrackSize{};
        Vec2  vTrack0{}, vTrackSize{};
        float hThumbX = 0.0f, hThumbW = 0.0f;
        float vThumbY = 0.0f, vThumbH = 0.0f;
        float hTrackRange = 0.0f, vTrackRange = 0.0f;  // px the thumb can travel
        float hCamMin = 0.0f, hCamMax = 0.0f;          // camera.x bounds (world)
        float vCamMin = 0.0f, vCamMax = 0.0f;          // camera.y bounds (world)
    };

    ScrollGeo ComputeScrollGeo() const {
        ScrollGeo g;
        if (m_map.Width() <= 0) return g;
        const Vec2 origin{kPaletteW, kToolbarH};
        const Vec2 size  {kScreenW - kPaletteW,
                          kScreenH - kToolbarH - kStatusH};
        constexpr float kBarT     = 8.0f;
        constexpr float kThumbMin = 24.0f;
        constexpr float kEps      = 0.5f;

        // Viewport rect in world units, map rect in world units, and the
        // bbox of the two. The bbox is the "reachable" world area; the
        // scrollbar is visible iff that area is bigger than the viewport.
        const float viewW = size.x / m_zoom;
        const float viewH = size.y / m_zoom;
        const float mapRightW  = float(m_map.Width()  * m_tileW);
        const float mapBottomW = float(m_map.Height() * m_tileH);
        const float hScrollMin = std::min(0.0f, m_camera.x);
        const float hScrollMax = std::max(mapRightW,  m_camera.x + viewW);
        const float vScrollMin = std::min(0.0f, m_camera.y);
        const float vScrollMax = std::max(mapBottomW, m_camera.y + viewH);
        const float hExtent = hScrollMax - hScrollMin;
        const float vExtent = vScrollMax - vScrollMin;

        const bool needH = hExtent > viewW + kEps;
        const bool needV = vExtent > viewH + kEps;
        const float barW = size.x - (needV ? kBarT : 0.0f);
        const float barH = size.y - (needH ? kBarT : 0.0f);
        if (needH) {
            const float by = origin.y + size.y - kBarT;
            g.hActive    = true;
            g.hTrack0    = {origin.x, by};
            g.hTrackSize = {barW, kBarT};
            g.hThumbW    = std::max(kThumbMin, viewW / hExtent * barW);
            g.hTrackRange = barW - g.hThumbW;
            g.hCamMin    = hScrollMin;
            g.hCamMax    = hScrollMax - viewW;
            const float camRange = g.hCamMax - g.hCamMin;
            const float t = camRange > 0.0f
                ? std::clamp((m_camera.x - g.hCamMin) / camRange, 0.0f, 1.0f)
                : 0.0f;
            g.hThumbX = origin.x + t * g.hTrackRange;
        }
        if (needV) {
            const float bx = origin.x + size.x - kBarT;
            g.vActive    = true;
            g.vTrack0    = {bx, origin.y};
            g.vTrackSize = {kBarT, barH};
            g.vThumbH    = std::max(kThumbMin, viewH / vExtent * barH);
            g.vTrackRange = barH - g.vThumbH;
            g.vCamMin    = vScrollMin;
            g.vCamMax    = vScrollMax - viewH;
            const float camRange = g.vCamMax - g.vCamMin;
            const float t = camRange > 0.0f
                ? std::clamp((m_camera.y - g.vCamMin) / camRange, 0.0f, 1.0f)
                : 0.0f;
            g.vThumbY = origin.y + t * g.vTrackRange;
        }
        return g;
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

        // Collision overlay. Visible when its layer is active, or when
        // overlays are on (default). Hidden in Tile/Object modes when the
        // user has pressed H to clear the canvas of non-active overlays.
        const bool showCollision = !m_collision.empty() &&
            (m_showCollision || m_mode == EditLayer::Collision);
        if (showCollision) {
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

        // Object markers. Same overlay gate as collision -- always shown
        // in Object mode, otherwise gated by m_layerOverlay (default on).
        const bool showObjects = !m_objects.empty() &&
            (m_showObjects || m_mode == EditLayer::Object);
        if (showObjects) {
            const float sx = float(m_tileW) * m_zoom;
            const float sy = float(m_tileH) * m_zoom;
            for (const auto& m : m_objects) {
                const Vec2 p = WorldToScreen(
                    {float(m.x * m_tileW), float(m.y * m_tileH)}, origin);
                // While dragging, dim the source marker so the user sees
                // a clear "from" preview alongside the ghost-at-hover.
                const bool isDragSrc = (m_objDragging &&
                                        m.x == m_objDragSrcTx &&
                                        m.y == m_objDragSrcTy);
                const Color bgC  = isDragSrc
                    ? Color(0.0f, 0.0f, 0.0f, 0.30f)
                    : Color(0.0f, 0.0f, 0.0f, 0.55f);
                const Color edgeC = isDragSrc
                    ? Color(1.0f, 0.86f, 0.20f, 0.45f)
                    : Color(1.0f, 0.86f, 0.20f, 1.0f);
                const Color textC = isDragSrc
                    ? Color(1.0f, 0.95f, 0.30f, 0.40f)
                    : Color(1.0f, 0.95f, 0.30f, 1.0f);
                r.DrawQuad(p, {sx, sy}, bgC);
                DrawRectOutline(r, p, {sx, sy}, 2.0f, edgeC);
                const char text[2] = { m.symbol, 0 };
                const float gh = std::min(sx, sy) * 0.6f;
                const float w  = MeasureMicroText(text, gh);
                DrawMicroText(r, text,
                              {p.x + (sx - w) * 0.5f,
                               p.y + (sy - gh) * 0.5f},
                              gh, textC);
                // Tiny yellow square in the top-right corner if the
                // marker carries non-empty data -- so the user can spot
                // metadata-bearing markers (doors, etc.) at a glance.
                if (!m.data.empty()) {
                    const float dotS = std::max(3.0f, sx * 0.10f);
                    const Color dotC = isDragSrc
                        ? Color(1.0f, 0.95f, 0.30f, 0.50f)
                        : Color(1.0f, 0.95f, 0.30f, 1.0f);
                    r.DrawQuad({p.x + sx - dotS - 3.0f, p.y + 3.0f},
                               {dotS, dotS}, dotC);
                }
            }
        }

        // Marker-drag ghost: draw a translucent copy of the source
        // marker at the hover cell so the user sees where it will land.
        // Hidden when the cursor strays outside the map.
        if (m_objDragging && m_objDragSym != 0 &&
            m_hoverTx >= 0 && m_hoverTx < m_map.Width() &&
            m_hoverTy >= 0 && m_hoverTy < m_map.Height() &&
            (m_hoverTx != m_objDragSrcTx ||
             m_hoverTy != m_objDragSrcTy)) {
            const float sx = float(m_tileW) * m_zoom;
            const float sy = float(m_tileH) * m_zoom;
            const Vec2 gp = WorldToScreen(
                {float(m_hoverTx * m_tileW),
                 float(m_hoverTy * m_tileH)}, origin);
            r.DrawQuad(gp, {sx, sy}, Color(0.0f, 0.0f, 0.0f, 0.55f));
            DrawRectOutline(r, gp, {sx, sy}, 2.0f,
                            Color(1.0f, 0.92f, 0.55f, 0.95f));
            const char gt[2] = { m_objDragSym, 0 };
            const float gh = std::min(sx, sy) * 0.6f;
            const float gw = MeasureMicroText(gt, gh);
            DrawMicroText(r, gt,
                          {gp.x + (sx - gw) * 0.5f,
                           gp.y + (sy - gh) * 0.5f},
                          gh, Color(1.0f, 0.95f, 0.30f, 0.95f));
        }

        // Stamp hover preview (Tile mode, multi-cell stamp). Draws a faint
        // copy of each stamp cell at the hover position so the user can
        // see exactly where the brush will land before clicking. Cells
        // outside the map are clipped; cells marked -1 (palette rect
        // overhanging the tileset end) are skipped, matching paint.
        if (m_mode == EditLayer::Tile &&
            m_hoverTx >= 0 && m_hoverTy >= 0 &&
            !m_stampTiles.empty() &&
            (m_stampW > 1 || m_stampH > 1)) {
            const float sx = float(m_tileW) * m_zoom;
            const float sy = float(m_tileH) * m_zoom;
            const Color tint(1.0f, 1.0f, 1.0f, 0.55f);
            for (int dy = 0; dy < m_stampH; ++dy) {
                for (int dx = 0; dx < m_stampW; ++dx) {
                    const int cx = m_hoverTx + dx;
                    const int cy = m_hoverTy + dy;
                    if (cx < 0 || cy < 0 ||
                        cx >= m_map.Width() || cy >= m_map.Height()) continue;
                    const int id =
                        m_stampTiles[size_t(dy) * m_stampW + size_t(dx)];
                    if (id < 0) continue;
                    const TextureRegion region = m_sheet.At(id);
                    if (!region.Valid()) continue;
                    const Vec2 p = WorldToScreen(
                        {float(cx * m_tileW), float(cy * m_tileH)}, origin);
                    r.DrawQuad(p, {sx, sy}, region, tint);
                }
            }
            const Vec2 a = WorldToScreen(
                {float(m_hoverTx * m_tileW),
                 float(m_hoverTy * m_tileH)}, origin);
            DrawRectOutline(r, a,
                            {sx * float(m_stampW), sy * float(m_stampH)},
                            2.0f, kHoverHi);
        } else if (m_hoverTx >= 0 && m_hoverTy >= 0 &&
                   m_hoverTx < m_map.Width() &&
                   m_hoverTy < m_map.Height()) {
            // Brush hover. brush=1 -> single cell outline; brush>1 ->
            // N-cell square centred on hover. Object mode forces 1
            // through EffectiveBrushSize().
            const int n = EffectiveBrushSize();
            const int rOff = (n - 1) / 2;
            const Vec2 hp = WorldToScreen(
                {float((m_hoverTx - rOff) * m_tileW),
                 float((m_hoverTy - rOff) * m_tileH)}, origin);
            DrawRectOutline(r, hp,
                            {float(m_tileW * n) * m_zoom,
                             float(m_tileH * n) * m_zoom},
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

        // Scrollbar overlay. Visible only when the canvas can't show the
        // full map -- either because the map is bigger than the canvas or
        // the camera has shifted some of it off-screen. Bars sit flush
        // against the right/bottom edges of the canvas; thumb width and
        // position mirror the visible window over the reachable world,
        // so the user can tell at a glance where they are in a big map.
        // Drag handled in OnUpdate via the same ComputeScrollGeo() result.
        constexpr float kBarPad = 1.0f;
        const Color trackC(1.0f, 1.0f, 1.0f, 0.10f);
        // Idle thumb sits dim against the dark canvas so it doesn't compete
        // with painted tiles; hover bumps to near-white + bright yellow tint
        // to telegraph "this is grabbable"; active drag locks at full
        // yellow so the user can see the bar is following the cursor.
        const Color thumbBaseC  (0.70f, 0.74f, 0.84f, 0.75f);
        const Color thumbHoverC (1.00f, 0.92f, 0.55f, 1.00f);
        const Color thumbActiveC(1.00f, 0.86f, 0.20f, 1.00f);
        const ScrollGeo g = ComputeScrollGeo();
        const Vec2 mouseNow = Input::MousePosition();
        if (g.hActive) {
            r.DrawQuad(g.hTrack0, g.hTrackSize, trackC);
            const bool hover =
                mouseNow.x >= g.hThumbX &&
                mouseNow.x <  g.hThumbX + g.hThumbW &&
                mouseNow.y >= g.hTrack0.y &&
                mouseNow.y <  g.hTrack0.y + g.hTrackSize.y;
            const Color& c = (m_scrollDrag == 1) ? thumbActiveC
                          : (hover            ) ? thumbHoverC
                                                : thumbBaseC;
            r.DrawQuad({g.hThumbX, g.hTrack0.y + kBarPad},
                       {g.hThumbW, g.hTrackSize.y - kBarPad * 2.0f}, c);
        }
        if (g.vActive) {
            r.DrawQuad(g.vTrack0, g.vTrackSize, trackC);
            const bool hover =
                mouseNow.y >= g.vThumbY &&
                mouseNow.y <  g.vThumbY + g.vThumbH &&
                mouseNow.x >= g.vTrack0.x &&
                mouseNow.x <  g.vTrack0.x + g.vTrackSize.x;
            const Color& c = (m_scrollDrag == 2) ? thumbActiveC
                          : (hover            ) ? thumbHoverC
                                                : thumbBaseC;
            r.DrawQuad({g.vTrack0.x + kBarPad, g.vThumbY},
                       {g.vTrackSize.x - kBarPad * 2.0f, g.vThumbH}, c);
        }

        // Resize edge feedback. Hover: thin highlight along the edge under
        // the cursor, hinting that it's grabbable. Drag: ghost outline of
        // the prospective new map bounds so the user can size by sight
        // before releasing. The actual resize commits in OnUpdate on
        // mouse-up via DoResize().
        const ResizeEdge visibleEdge = m_resizingMap
            ? m_resizingEdge : m_resizeHoverEdge;
        if (visibleEdge != ResizeEdge::None) {
            const Vec2 tl = WorldToScreen({0.0f, 0.0f}, origin);
            const Vec2 br = WorldToScreen(
                {float(m_map.Width()  * m_tileW),
                 float(m_map.Height() * m_tileH)}, origin);
            constexpr float kEdgeThickness = 3.0f;
            const Color cEdge = m_resizingMap
                ? Color(1.00f, 0.86f, 0.20f, 1.0f)    // active = saturated
                : Color(1.00f, 0.92f, 0.55f, 0.85f);  // hover = soft yellow
            switch (visibleEdge) {
                case ResizeEdge::Left:
                    r.DrawQuad({tl.x - kEdgeThickness * 0.5f, tl.y},
                               {kEdgeThickness, br.y - tl.y}, cEdge); break;
                case ResizeEdge::Right:
                    r.DrawQuad({br.x - kEdgeThickness * 0.5f, tl.y},
                               {kEdgeThickness, br.y - tl.y}, cEdge); break;
                case ResizeEdge::Top:
                    r.DrawQuad({tl.x, tl.y - kEdgeThickness * 0.5f},
                               {br.x - tl.x, kEdgeThickness}, cEdge); break;
                case ResizeEdge::Bottom:
                    r.DrawQuad({tl.x, br.y - kEdgeThickness * 0.5f},
                               {br.x - tl.x, kEdgeThickness}, cEdge); break;
                case ResizeEdge::None: break;
            }
        }
        if (m_resizingMap) {
            const int delta = ComputeResizeDelta(m_resizingEdge, mouseNow);
            const int W = m_map.Width(), H = m_map.Height();
            // Ghost rect = new map bounds expressed in OLD-grid screen
            // coords so the user sees the boundary move under the cursor
            // (camera doesn't shift during drag). For Left/Top grows, the
            // new map's origin moves outward by delta cells from where 0,0
            // is currently drawn.
            float gx = 0.0f, gy = 0.0f;
            float gw = float(W), gh = float(H);
            switch (m_resizingEdge) {
                case ResizeEdge::Right:  gw += float(delta); break;
                case ResizeEdge::Bottom: gh += float(delta); break;
                case ResizeEdge::Left:
                    gx -= float(delta); gw += float(delta); break;
                case ResizeEdge::Top:
                    gy -= float(delta); gh += float(delta); break;
                case ResizeEdge::None: break;
            }
            const Vec2 ga = WorldToScreen(
                {gx * float(m_tileW), gy * float(m_tileH)}, origin);
            DrawRectOutline(r, ga,
                            {gw * float(m_tileW) * m_zoom,
                             gh * float(m_tileH) * m_zoom},
                            2.0f, kSelectHi);
            // "WxH" hint next to the cursor.
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0fx%.0f", gw, gh);
            DrawMicroText(r, buf,
                          {mouseNow.x + 14.0f, mouseNow.y + 14.0f},
                          14.0f, kSelectHi);
        }

        // Clip — visually mark the canvas border so it's clear where the
        // panel ends regardless of camera offset.
        DrawRectOutline(r, origin, size, 2.0f, kCanvasOut);

        // Cell-info tooltip near the cursor. Skipped while a marker drag
        // ghost is following the cursor (the two would stack on top of
        // each other) and when the cursor is off the map.
        if (m_inCanvas && !m_objDragging &&
            m_hoverTx >= 0 && m_hoverTx < m_map.Width() &&
            m_hoverTy >= 0 && m_hoverTy < m_map.Height()) {
            DrawHoverTooltip(r, mouseNow);
        }

        // Minimap on top of everything inside the canvas panel so the
        // viewport box stays readable over busy tiles.
        DrawMinimap(r);
    }

    // Floating tooltip next to the cursor showing the hovered cell's
    // coords + per-layer state (tile id, collision code, marker symbol).
    // Clamped to the canvas bounds so it never escapes off-screen.
    void DrawHoverTooltip(Renderer2D& r, const Vec2& mouse) {
        std::vector<std::string> lines;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Cell: %d, %d",
                      m_hoverTx, m_hoverTy);
        lines.emplace_back(buf);

        const int tile = m_map.GetTile(m_hoverTx, m_hoverTy);
        std::snprintf(buf, sizeof(buf), "Tile: %s",
                      tile >= 0 ? std::to_string(tile).c_str()
                                : "(empty)");
        lines.emplace_back(buf);

        if (!m_collision.empty()) {
            const size_t idx = size_t(m_hoverTy) *
                               size_t(m_map.Width()) +
                               size_t(m_hoverTx);
            if (idx < m_collision.size()) {
                static const char* kCollNames[] = {
                    "Floor", "Wall", "Water"
                };
                const uint8_t c = m_collision[idx];
                std::snprintf(buf, sizeof(buf), "Coll: %s",
                              c < 3 ? kCollNames[c] : "?");
                lines.emplace_back(buf);
            }
        }

        const int sym = GetObjectSymbol(m_hoverTx, m_hoverTy);
        if (sym != 0) {
            const char* label = "?";
            for (const auto& s : m_symbols) {
                if (s.symbol == char(sym)) {
                    label = s.label.empty() ? "(no label)"
                                            : s.label.c_str();
                    break;
                }
            }
            std::snprintf(buf, sizeof(buf), "Marker: %c (%s)",
                          char(sym), label);
            lines.emplace_back(buf);
            // Append metadata if present -- lets the user verify door
            // destinations / item params without opening the modal.
            for (const auto& mo : m_objects) {
                if (mo.x == m_hoverTx && mo.y == m_hoverTy &&
                    !mo.data.empty()) {
                    std::snprintf(buf, sizeof(buf), "Data: %s",
                                  mo.data.c_str());
                    lines.emplace_back(buf);
                    break;
                }
            }
        }

        constexpr float lineH = 16.0f;
        constexpr float pad   = 8.0f;
        constexpr float pixel = 12.0f;
        float maxW = 0.0f;
        for (const auto& l : lines)
            maxW = std::max(maxW, MeasureMicroText(l, pixel));
        const float panelW = maxW + pad * 2.0f;
        const float panelH = float(lines.size()) * lineH + pad * 2.0f;

        // Offset toward bottom-right of cursor; flip to other side when
        // the tooltip would leave the canvas viewport.
        const float canvasRight  = kScreenW;
        const float canvasBottom = kScreenH - kStatusH;
        Vec2 p{mouse.x + 18.0f, mouse.y + 18.0f};
        if (p.x + panelW > canvasRight)
            p.x = mouse.x - 18.0f - panelW;
        if (p.y + panelH > canvasBottom)
            p.y = mouse.y - 18.0f - panelH;

        r.DrawQuad(p, {panelW, panelH},
                   Color(0.05f, 0.06f, 0.08f, 0.88f));
        DrawRectOutline(r, p, {panelW, panelH}, 1.0f,
                        Color(1.0f, 0.86f, 0.20f, 0.60f));
        for (size_t i = 0; i < lines.size(); ++i) {
            DrawMicroText(r, lines[i],
                          {p.x + pad,
                           p.y + pad + float(i) * lineH},
                          pixel, kStatusText);
        }
    }

    void DrawMinimap(Renderer2D& r) {
        const MinimapGeo g = ComputeMinimap();
        if (!g.active) return;
        // Background + outline. Slightly darker than kPaneBg so the
        // minimap reads as "info panel" rather than another canvas.
        const Color bg(0.10f, 0.11f, 0.14f, 0.92f);
        r.DrawQuad({g.origin.x - 4.0f, g.origin.y - 4.0f},
                   {g.size.x + 8.0f, g.size.y + 8.0f}, bg);
        DrawRectOutline(r, {g.origin.x - 4.0f, g.origin.y - 4.0f},
                        {g.size.x + 8.0f, g.size.y + 8.0f}, 1.0f,
                        Color(0.0f, 0.0f, 0.0f, 0.60f));
        // Tile thumbnails. Cells with -1 stay as the background dark.
        // Use the same SpriteSheet regions so tile colour matches the
        // canvas at a glance -- swap a brick tile and the minimap
        // updates the same frame.
        const float c = g.cellSize;
        for (int ty = 0; ty < m_map.Height(); ++ty) {
            for (int tx = 0; tx < m_map.Width(); ++tx) {
                const int id = m_map.GetTile(tx, ty);
                if (id < 0) continue;
                const TextureRegion region = m_sheet.At(id);
                if (!region.Valid()) continue;
                r.DrawQuad({g.origin.x + float(tx) * c,
                            g.origin.y + float(ty) * c},
                           {c, c}, region);
            }
        }
        // Viewport indicator. The visible rect [m_camera, +viewSize] in
        // world units maps to the minimap by dividing by (map_dim) and
        // scaling by minimap_size. Clamped + drawn as an outline so the
        // tile thumbnails stay readable underneath.
        const float viewW = (kScreenW - kPaletteW) / m_zoom;
        const float viewH = (kScreenH - kToolbarH - kStatusH) / m_zoom;
        const float worldW = float(m_map.Width()  * m_tileW);
        const float worldH = float(m_map.Height() * m_tileH);
        const float vx0 = std::clamp(m_camera.x / worldW,           0.0f, 1.0f);
        const float vy0 = std::clamp(m_camera.y / worldH,           0.0f, 1.0f);
        const float vx1 = std::clamp((m_camera.x + viewW) / worldW, 0.0f, 1.0f);
        const float vy1 = std::clamp((m_camera.y + viewH) / worldH, 0.0f, 1.0f);
        DrawRectOutline(r,
            {g.origin.x + vx0 * g.size.x, g.origin.y + vy0 * g.size.y},
            {(vx1 - vx0) * g.size.x, (vy1 - vy0) * g.size.y},
            1.5f, kSelectHi);
    }

    // Hit-test the palette tab strip. Returns 0/1/2 for Tile/Collision/
    // Object respectively, or -1 if the cursor is outside any tab cell.
    int TabIndexAt(Vec2 mouse) const {
        if (mouse.y < kToolbarH || mouse.y >= kToolbarH + kTabStripH)
            return -1;
        if (mouse.x < 0.0f || mouse.x >= kPaletteW) return -1;
        const float tabW = kPaletteW / 3.0f;
        const int idx = int(std::floor(mouse.x / tabW));
        return std::clamp(idx, 0, 2);
    }

    void DrawTabStrip(Renderer2D& r, Vec2 mouse) {
        constexpr float kTabPad = 2.0f;
        const float tabW = kPaletteW / 3.0f;
        const float tabY = kToolbarH;
        // Short labels fit a 256/3 ≈ 85 px tab at the editor's MSDF font
        // size without truncating; full names would overflow into the
        // neighbour. "OBJ" reads as object alongside "TILE" and "COLL".
        static const char* kLabels[] = { "TILE", "COLL", "OBJ" };
        const int hover = TabIndexAt(mouse);
        for (int i = 0; i < 3; ++i) {
            const float tx = float(i) * tabW;
            const bool active = (i == int(m_mode));
            const bool isHover = (i == hover);
            // Background tint: active matches the per-mode content panel
            // so the tab visually merges with what's below; inactive is
            // slightly darker than the toolbar to read as a separate
            // surface; hover brightens to telegraph the click.
            Color bg = active   ? kPaneBg
                     : isHover  ? Color(0.24f, 0.26f, 0.32f, 1.0f)
                                : Color(0.16f, 0.17f, 0.21f, 1.0f);
            r.DrawQuad({tx + kTabPad, tabY},
                       {tabW - kTabPad * 2.0f, kTabStripH}, bg);
            // Top accent stripe on the active tab; matches kSelectHi yellow
            // so the eye lands here at a glance.
            if (active) {
                r.DrawQuad({tx + kTabPad, tabY},
                           {tabW - kTabPad * 2.0f, 2.0f}, kSelectHi);
            }
            const float pixel = 14.0f;
            const std::string lbl = kLabels[i];
            const float lw = MeasureMicroText(lbl, pixel);
            const Color textC = active   ? kSelectHi
                              : isHover  ? Color(0.95f, 0.96f, 0.98f, 1.0f)
                                         : kStatusText;
            DrawMicroText(r, lbl,
                          {tx + (tabW - lw) * 0.5f,
                           tabY + (kTabStripH - pixel) * 0.5f},
                          pixel, textC);
        }
        // Bottom border line so the strip reads as separate from the
        // content area when the active tab matches its background.
        r.DrawQuad({0.0f, tabY + kTabStripH - 1.0f},
                   {kPaletteW, 1.0f}, Color(0.0f, 0.0f, 0.0f, 0.55f));
    }

    void DrawPalettePanel(Renderer2D& r) {
        const Vec2 panelOrigin{0.0f, kToolbarH};
        const Vec2 panelSize  {kPaletteW, kScreenH - kToolbarH - kStatusH};
        r.DrawQuad(panelOrigin, panelSize, kPaneBg);

        const Vec2 mouse  = Input::MousePosition();
        DrawTabStrip(r, mouse);

        // Per-mode content sits below the tab strip.
        const Vec2 origin{0.0f, kToolbarH + kTabStripH};
        const Vec2 size  {kPaletteW,
                          kScreenH - kToolbarH - kStatusH - kTabStripH};

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
        }
        // Single outline around the whole stamp rect (1x1 = the current
        // selected tile; >1x1 = the rectangle dragged in the palette).
        if (cols > 0 && m_selectedTile >= 0 && m_selectedTile < total) {
            const int tlX = m_selectedTile % cols;
            const int tlY = m_selectedTile / cols;
            const float px = origin.x + pad + tlX * pitch;
            const float py = origin.y + pad + tlY * pitch - m_paletteScroll;
            const float w  = pitch * m_stampW - 4.0f;
            const float h  = pitch * m_stampH - 4.0f;
            DrawRectOutline(r, {px - 1.0f, py - 1.0f},
                            {w + 2.0f, h + 2.0f},
                            2.0f, kSelectHi);
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
    // Default fallback set when a .ntmap has no `symbols` section. Once a
    // map is saved, its on-disk symbols list takes precedence over this on
    // subsequent loads. Each entry: (char, 0xRRGGBB tint, label).
    // P/E/I/C/X are the brawl-style generic set; M/g/!/* belong to the
    // Mario port. A game still only consumes the bare char -- the colour
    // and label are editor-only metadata.
    static std::vector<SymbolDef> DefaultObjectSymbols() {
        return {
            {'P', 0x55b8ffu, "player"},
            {'E', 0xff5050u, "enemy"},
            {'I', 0xffd060u, "item"},
            {'C', 0xffe066u, "coin"},
            {'X', 0xa0a0a0u, "marker"},
            {'g', 0xa06a3cu, "goomba"},
            {'!', 0xff9966u, "mushroom"},
            {'*', 0xff5577u, "fire flower"},
        };
    }

    static Color ColorFromHex(uint32_t rgb) {
        const float r = float((rgb >> 16) & 0xFFu) / 255.0f;
        const float g = float((rgb >>  8) & 0xFFu) / 255.0f;
        const float b = float( rgb        & 0xFFu) / 255.0f;
        return Color(r, g, b, 1.0f);
    }

    const char* ObjectLabel(char sym) const {
        for (const auto& s : m_symbols)
            if (s.symbol == sym) return s.label.c_str();
        return "marker";
    }

    void DrawObjectPalette(Renderer2D& r, Vec2 origin) {
        const Vec2 mouseNow = Input::MousePosition();
        DrawMicroText(r, "object layer",
                      {origin.x + 12.0f, origin.y + 8.0f},
                      14.0f, kStatusText);

        // "+ Add symbol" button. Hover highlight gives the affordance;
        // click handler lives in OnUpdate via ObjectAddHit so this draw
        // function is purely visual.
        {
            const Vec2 bp = ObjAddBtnRect(origin);
            const Vec2 bs = ObjAddBtnSize();
            const bool over = IsInside(mouseNow, bp, bs);
            const Color bg = over ? Color(0.30f, 0.34f, 0.42f, 1.0f)
                                  : Color(0.22f, 0.24f, 0.30f, 1.0f);
            r.DrawQuad(bp, bs, bg);
            DrawRectOutline(r, bp, bs, 1.0f, Color(0.0f, 0.0f, 0.0f, 0.6f));
            const std::string lbl = "+ Add symbol";
            const float lw = MeasureMicroText(lbl, 14.0f);
            DrawMicroText(r, lbl,
                          {bp.x + (bs.x - lw) * 0.5f,
                           bp.y + (bs.y - 14.0f) * 0.5f},
                          14.0f, over ? kSelectHi : kStatusText);
        }

        constexpr float pad   = 12.0f;
        constexpr float cellH = 56.0f;
        constexpr float gap   = 8.0f;
        for (size_t i = 0; i < m_symbols.size(); ++i) {
            const SymbolDef& s = m_symbols[i];
            const float py = origin.y + kObjListTop +
                             float(i) * (cellH + gap);
            const Vec2 p{origin.x + pad, py};
            const Vec2 sz{kPaletteW - pad * 2.0f, cellH};
            // Cell background is the neutral pane tint; a thin coloured
            // strip on the left identifies the symbol without drowning
            // the white label text. Matches the rest of the palette UI.
            r.DrawQuad(p, sz, Color(0.20f, 0.22f, 0.30f, 1.0f));
            r.DrawQuad(p, {6.0f, sz.y}, ColorFromHex(s.color));
            DrawRectOutline(r, p, sz, 1.0f, Color(0.0f, 0.0f, 0.0f, 0.6f));
            const char text[2] = { s.symbol, 0 };
            DrawMicroText(r, text,
                          {p.x + 16.0f, p.y + cellH * 0.5f - 12.0f},
                          24.0f, ColorFromHex(s.color));
            DrawMicroText(r, s.label.c_str(),
                          {p.x + 54.0f, p.y + cellH * 0.5f - 8.0f},
                          14.0f, kStatusText);

            // × delete button on the right side of the cell, vertically
            // centred so it lines up with the big symbol glyph and the
            // label text instead of stranding at the cell's top edge.
            // Transparent in the resting state so it doesn't fight the
            // cell tint; hovered state swaps the glyph to kSelectHi
            // yellow so the affordance reads as "armed".
            const float bx = p.x + sz.x - kObjDelBtnSize - kObjDelBtnPad;
            const float by = p.y + (cellH - kObjDelBtnSize) * 0.5f;
            const bool overDel = mouseNow.x >= bx &&
                mouseNow.x < bx + kObjDelBtnSize &&
                mouseNow.y >= by &&
                mouseNow.y < by + kObjDelBtnSize;
            const float gx = MeasureMicroText("x", 14.0f);
            DrawMicroText(r, "x",
                          {bx + (kObjDelBtnSize - gx) * 0.5f,
                           by + (kObjDelBtnSize - 14.0f) * 0.5f},
                          14.0f,
                          overDel ? kSelectHi
                                  : Color(0.75f, 0.78f, 0.85f, 0.70f));

            if (s.symbol == m_selectedSym) {
                DrawRectOutline(r, {p.x - 2.0f, p.y - 2.0f},
                                {sz.x + 4.0f, sz.y + 4.0f},
                                2.0f, kSelectHi);
            }
        }
    }

    // Per-cell × button dimensions; shared by hit test and draw so they
    // stay in lockstep.
    static constexpr float kObjDelBtnSize = 18.0f;
    static constexpr float kObjDelBtnPad  = 6.0f;
    // "+ Add symbol" button at the top of the Object palette.
    static constexpr float kObjAddBtnH    = 28.0f;
    static constexpr float kObjAddBtnTop  = 32.0f;  // from origin.y
    static constexpr float kObjListTop    = kObjAddBtnTop + kObjAddBtnH + 8.0f;

    // Swatches used by the Add Symbol modal. Eight colours that read well
    // on the editor's dark cells; the user picks one by clicking. Default
    // (kObjAddDefault) is a neutral grey so a brand-new symbol is visible
    // before the user changes anything.
    static constexpr uint32_t kObjAddSwatches[8] = {
        0x55b8ffu, 0xff5050u, 0xffd060u, 0x66cc44u,
        0xff9966u, 0xff5577u, 0x44ccccu, 0xeeeeeeu,
    };

    Vec2 ObjAddBtnRect(Vec2 origin) const {
        return {origin.x + 12.0f, origin.y + kObjAddBtnTop};
    }
    Vec2 ObjAddBtnSize() const {
        return {kPaletteW - 24.0f, kObjAddBtnH};
    }
    bool ObjectAddHit(Vec2 mouse, Vec2 origin) const {
        return IsInside(mouse, ObjAddBtnRect(origin), ObjAddBtnSize());
    }

    // Per-cell × button position (centred vertically inside the cell on
    // the right edge). Returns the symbol index it would delete, or -1
    // if the cursor isn't over one.
    int ObjectDeleteHit(Vec2 mouse, Vec2 origin) const {
        constexpr float pad   = 12.0f;
        constexpr float cellH = 56.0f;
        constexpr float gap   = 8.0f;
        for (size_t i = 0; i < m_symbols.size(); ++i) {
            const float py = origin.y + kObjListTop +
                             float(i) * (cellH + gap);
            const float bx = origin.x + pad + (kPaletteW - pad * 2.0f) -
                             kObjDelBtnSize - kObjDelBtnPad;
            const float by = py + (cellH - kObjDelBtnSize) * 0.5f;
            if (mouse.x >= bx && mouse.x < bx + kObjDelBtnSize &&
                mouse.y >= by && mouse.y < by + kObjDelBtnSize) {
                return int(i);
            }
        }
        return -1;
    }

    int ObjectPaletteHit(Vec2 mouse, Vec2 origin) const {
        constexpr float pad   = 12.0f;
        constexpr float cellH = 56.0f;
        constexpr float gap   = 8.0f;
        // Avoid latching selection when the press is actually on the
        // delete button -- the dispatch in OnUpdate already routes that
        // separately, but a press near the top-right corner of a cell
        // should never accidentally select either.
        if (ObjectDeleteHit(mouse, origin) >= 0) return -1;
        for (size_t i = 0; i < m_symbols.size(); ++i) {
            const float py = origin.y + kObjListTop +
                             float(i) * (cellH + gap);
            const float px = origin.x + pad;
            if (mouse.x >= px && mouse.x < px + (kPaletteW - pad * 2.0f) &&
                mouse.y >= py && mouse.y < py + cellH) {
                return int(i);
            }
        }
        return -1;
    }

    // Drop one entry from m_symbols AND erase every marker that uses its
    // char. The map dirties so the change makes it into the next save;
    // selection falls back to the previous-or-first entry so the user
    // isn't stuck pointing at a vanished symbol.
    void DeleteSymbolAt(int idx) {
        if (idx < 0 || size_t(idx) >= m_symbols.size()) return;
        const char sym = m_symbols[size_t(idx)].symbol;
        m_symbols.erase(m_symbols.begin() + idx);
        m_objects.erase(
            std::remove_if(m_objects.begin(), m_objects.end(),
                [sym](const ObjectMarker& m) { return m.symbol == sym; }),
            m_objects.end());
        if (m_selectedSym == sym) {
            m_selectedSym = m_symbols.empty()
                ? 0
                : m_symbols[size_t(std::min(idx,
                    int(m_symbols.size()) - 1))].symbol;
        }
        m_dirty = true;
    }

    int CountMarkersWithSymbol(char sym) const {
        int n = 0;
        for (const auto& m : m_objects)
            if (m.symbol == sym) ++n;
        return n;
    }

    static std::string ColorToHex(uint32_t rgb) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%06x", unsigned(rgb & 0xFFFFFFu));
        return std::string(buf);
    }

    void OpenAddSymbolModal() {
        m_addSymbolModal = true;
        m_addChar  = 0;
        m_addColor = kObjAddSwatches[0];
        m_addLabel.clear();
        m_addHex   = ColorToHex(m_addColor);
        m_addFocus = 0;
    }
    void CommitAddSymbol() {
        if (m_addChar == 0) return;
        // Reject duplicates: the editor identifies symbols by char, so
        // two entries with the same char would make selection ambiguous
        // and confuse the delete path.
        for (const auto& s : m_symbols)
            if (s.symbol == m_addChar) { CloseAddSymbol(); return; }
        m_symbols.push_back({m_addChar, m_addColor, m_addLabel});
        m_selectedSym = m_addChar;
        m_dirty = true;
        CloseAddSymbol();
    }
    void CloseAddSymbol() {
        m_addSymbolModal = false;
        m_addChar = 0;
        m_addLabel.clear();
        m_addHex.clear();
        m_addFocus = 0;
    }

    // Shared add-modal geometry. DrawAddSymbolModal and the OnUpdate
    // hit-tests both go through here so a layout edit in one place
    // automatically syncs the other.
    struct AddModalGeo {
        Vec2  panel;
        float panelW = 520.0f;
        float panelH = 340.0f;
        float swatchRowY = 0.0f;
        float swatchSize = 36.0f;
        float swatchGap  = 4.0f;
        float swatchStartX = 0.0f;
        Vec2  hexP{}, hexS{};
        Vec2  lblP{}, lblS{};
    };

    AddModalGeo ComputeAddModalGeo() const {
        AddModalGeo g;
        g.panel = {(kScreenW - g.panelW) * 0.5f,
                   (kScreenH - g.panelH) * 0.5f};
        g.swatchRowY    = g.panel.y + 130.0f;
        const float total = g.swatchSize * 8 + g.swatchGap * 7;
        g.swatchStartX  = g.panel.x + (g.panelW - total) * 0.5f;
        g.hexP = {g.panel.x + 110.0f, g.panel.y + 186.0f};
        g.hexS = {180.0f, 32.0f};
        g.lblP = {g.panel.x + 110.0f, g.panel.y + 242.0f};
        g.lblS = {g.panelW - 134.0f, 32.0f};
        return g;
    }

    int AddModalSwatchHit(Vec2 mouse) const {
        const AddModalGeo g = ComputeAddModalGeo();
        if (mouse.y < g.swatchRowY ||
            mouse.y >= g.swatchRowY + g.swatchSize) return -1;
        for (int i = 0; i < 8; ++i) {
            const float sx = g.swatchStartX +
                             float(i) * (g.swatchSize + g.swatchGap);
            if (mouse.x >= sx && mouse.x < sx + g.swatchSize) return i;
        }
        return -1;
    }

    // 0 = label box, 1 = hex box, -1 = neither.
    int AddModalFieldHit(Vec2 mouse) const {
        const AddModalGeo g = ComputeAddModalGeo();
        if (IsInside(mouse, g.lblP, g.lblS)) return 0;
        if (IsInside(mouse, g.hexP, g.hexS)) return 1;
        return -1;
    }

    static bool IsHexDigit(char c) {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    }

    // Parse the hex string -> update m_addColor when there are exactly
    // six valid hex chars. Otherwise leave m_addColor alone so the
    // swatch / preview stays at the last sane value.
    void TryParseHex() {
        if (m_addHex.size() != 6) return;
        for (char c : m_addHex) if (!IsHexDigit(c)) return;
        const unsigned long v = std::strtoul(m_addHex.c_str(),
                                             nullptr, 16);
        m_addColor = uint32_t(v & 0xFFFFFFu);
    }

    // Modal cheatsheet panel. Trigger from F1, dismiss with F1/Esc. All
    // other input is suppressed in OnUpdate while m_helpOverlay is true,
    // so the cheatsheet doubles as a "pause and read" surface. Content is
    // grouped by purpose (FILE / EDIT / PAINT / SELECTION on the left;
    // CAMERA / VIEW / PALETTE / RESIZE on the right) instead of by key
    // family because users look up "how do I do X?" not "what does ctrl-X
    // do?".
    void DrawHelpOverlay(Renderer2D& r) {
        struct Row { const char* key; const char* desc; };
        struct Section {
            const char* title;
            const Row*  rows;
            int         rowCount;
        };
        static const Row kFile[] = {
            {"Ctrl+O",       "open .ntmap / .tmx"},
            {"Ctrl+N",       "new map from tileset"},
            {"Ctrl+R",       "recent files (1..5)"},
            {"Ctrl+S",       "save (.ntmap)"},
            {"Ctrl+Shift+S", "save as / rename"},
            {"Esc",          "close (or back out)"},
        };
        static const Row kEdit[] = {
            {"Ctrl+Z",       "undo"},
            {"Ctrl+Y",       "redo"},
            {"Ctrl+Shift+Z", "redo"},
        };
        static const Row kPaint[] = {
            {"LMB",         "paint with current value"},
            {"RMB",         "erase / reset"},
            {"Shift+drag",  "fill rectangle"},
            {"Alt+LMB",     "eyedropper"},
            {"F",           "flood fill same value"},
            {"[ / ]",       "brush size (1..9, odd)"},
            {"T",           "(Tile) auto-tile mode on/off"},
            {"LMB drag",    "(Object) move existing marker"},
        };
        static const Row kSelect[] = {
            {"Ctrl+drag",  "marquee select"},
            {"Ctrl+C/X/V", "copy / cut / paste"},
            {"Esc",        "clear selection"},
        };
        static const Row kCamera[] = {
            {"Arrows",       "pan"},
            {"Middle drag",  "pan"},
            {"Wheel, +/-",   "zoom at cursor"},
            {"Ctrl+0",       "zoom: fit to canvas"},
            {"Ctrl+1/2/4",   "zoom: 100% / 200% / 400%"},
            {"Home",         "reset camera and zoom"},
            {"Scroll drag",  "drag thumb to scroll"},
            {"Scroll track", "click empty area for page jump"},
            {"Minimap LMB",  "jump camera to spot"},
        };
        static const Row kView[] = {
            {"G",         "toggle grid"},
            {"H",         "toggle both non-active overlays"},
            {"Shift+1",   "toggle collision overlay"},
            {"Shift+2",   "toggle object overlay"},
            {"Tab or L",  "cycle Tile / Collision / Object"},
        };
        static const Row kPalette[] = {
            {"1..9",       "pick first 9 entries"},
            {"Drag (Tile)", "build multi-cell stamp"},
        };
        static const Row kResize[] = {
            {"Ctrl+arrow",       "grow that edge"},
            {"Ctrl+Shift+arrow", "shrink that edge"},
            {"Drag map edge",    "live resize"},
        };
        static const Section kCol1[] = {
            {"FILE",      kFile,   int(sizeof(kFile)   / sizeof(kFile[0]))},
            {"EDIT",      kEdit,   int(sizeof(kEdit)   / sizeof(kEdit[0]))},
            {"PAINT",     kPaint,  int(sizeof(kPaint)  / sizeof(kPaint[0]))},
            {"SELECTION", kSelect, int(sizeof(kSelect) / sizeof(kSelect[0]))},
        };
        static const Section kCol2[] = {
            {"CAMERA",  kCamera,  int(sizeof(kCamera)  / sizeof(kCamera[0]))},
            {"VIEW",    kView,    int(sizeof(kView)    / sizeof(kView[0]))},
            {"PALETTE", kPalette, int(sizeof(kPalette) / sizeof(kPalette[0]))},
            {"RESIZE",  kResize,  int(sizeof(kResize)  / sizeof(kResize[0]))},
        };

        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH},
                   Color(0.0f, 0.0f, 0.0f, 0.65f));
        constexpr float panelW = 880.0f;
        constexpr float panelH = 620.0f;
        const Vec2 panel{(kScreenW - panelW) * 0.5f,
                         (kScreenH - panelH) * 0.5f};
        r.DrawQuad(panel, {panelW, panelH}, kPaneBg);
        DrawRectOutline(r, panel, {panelW, panelH}, 2.0f, kSelectHi);

        DrawMicroText(r, "KEYBINDS",
                      {panel.x + 32.0f, panel.y + 24.0f},
                      28.0f, kSelectHi);
        const std::string hint = "F1 or Esc to close";
        const float hintW = MeasureMicroText(hint, 14.0f);
        DrawMicroText(r, hint,
                      {panel.x + panelW - hintW - 32.0f,
                       panel.y + 36.0f},
                      14.0f, kStatusText);

        constexpr float colY0   = 80.0f;     // from panel.y
        constexpr float colW    = (panelW - 64.0f) * 0.5f;
        const float col1X = panel.x + 32.0f;
        const float col2X = panel.x + 32.0f + colW;
        constexpr float rowH    = 20.0f;
        constexpr float headRowH = 24.0f;
        constexpr float sectionGap = 14.0f;
        constexpr float keyColW = 178.0f;
        const Color kKeyText (0.85f, 0.90f, 0.98f, 1.0f);

        auto DrawColumn = [&](float colX, const Section* secs, int n) {
            float y = panel.y + colY0;
            for (int i = 0; i < n; ++i) {
                DrawMicroText(r, secs[i].title, {colX, y}, 16.0f, kSelectHi);
                y += headRowH;
                for (int rIdx = 0; rIdx < secs[i].rowCount; ++rIdx) {
                    const Row& row = secs[i].rows[rIdx];
                    DrawMicroText(r, row.key,
                                  {colX + 8.0f, y}, 13.0f, kKeyText);
                    DrawMicroText(r, row.desc,
                                  {colX + 8.0f + keyColW, y}, 13.0f,
                                  kStatusText);
                    y += rowH;
                }
                y += sectionGap;
            }
        };
        DrawColumn(col1X, kCol1, int(sizeof(kCol1) / sizeof(kCol1[0])));
        DrawColumn(col2X, kCol2, int(sizeof(kCol2) / sizeof(kCol2[0])));
    }

    // Recent-files overlay. Lists what PushRecent() has captured -- most
    // recent first -- prefixed with 1..N so the user can pick by number
    // without reaching for the mouse. Empty list shows a short hint
    // instead of a dead panel so first-launch isn't confusing.
    void DrawRecentOverlay(Renderer2D& r) {
        constexpr float panelW = 720.0f;
        constexpr float panelH = 360.0f;
        const Vec2 p{(kScreenW - panelW) * 0.5f,
                     (kScreenH - panelH) * 0.5f};
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH},
                   Color(0.0f, 0.0f, 0.0f, 0.65f));
        r.DrawQuad(p, {panelW, panelH}, kPaneBg);
        DrawRectOutline(r, p, {panelW, panelH}, 2.0f, kSelectHi);
        DrawMicroText(r, "Recent files",
                      {p.x + 24.0f, p.y + 22.0f}, 22.0f, kSelectHi);
        DrawMicroText(r, "Ctrl+R or Esc to close",
                      {p.x + panelW - 230.0f, p.y + 32.0f},
                      14.0f, kStatusText);

        const Color kKeyText(0.85f, 0.90f, 0.98f, 1.0f);
        const float rowY0 = p.y + 76.0f;
        const float rowH  = 36.0f;
        if (m_recent.empty()) {
            DrawMicroText(r,
                "No recent files yet. Open something with Ctrl+O.",
                {p.x + 32.0f, rowY0},
                14.0f, kStatusText);
            return;
        }
        for (size_t i = 0; i < m_recent.size(); ++i) {
            const float y = rowY0 + float(i) * rowH;
            char key[4];
            std::snprintf(key, sizeof(key), "%zu", i + 1);
            DrawMicroText(r, key, {p.x + 32.0f, y}, 18.0f, kKeyText);
            // Truncate long paths from the LEFT so the leaf filename
            // (the part the user actually recognises) is always
            // visible at the right edge of the row.
            const std::string& full = m_recent[i];
            constexpr size_t kMax = 86;
            const std::string shown = full.size() > kMax
                ? "..." + full.substr(full.size() - kMax + 3)
                : full;
            DrawMicroText(r, shown, {p.x + 68.0f, y + 2.0f},
                          14.0f, kStatusText);
        }
    }

    // Delete-symbol confirm panel. Spells out the consequences (how many
    // markers will disappear) so a tap on the wrong × isn't an
    // irrecoverable loss.
    void DrawDeleteSymbolModal(Renderer2D& r) {
        if (size_t(m_deleteSymbolIdx) >= m_symbols.size()) return;
        const SymbolDef& s = m_symbols[size_t(m_deleteSymbolIdx)];
        const int n = CountMarkersWithSymbol(s.symbol);
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH},
                   Color(0.0f, 0.0f, 0.0f, 0.70f));
        constexpr float panelW = 540.0f;
        constexpr float panelH = 220.0f;
        const Vec2 p{(kScreenW - panelW) * 0.5f,
                     (kScreenH - panelH) * 0.5f};
        r.DrawQuad(p, {panelW, panelH}, kPaneBg);
        DrawRectOutline(r, p, {panelW, panelH}, 2.0f, kSelectHi);
        DrawMicroText(r, "Delete symbol",
                      {p.x + 24.0f, p.y + 22.0f}, 22.0f, kSelectHi);
        char line[160];
        std::snprintf(line, sizeof(line),
                      "Remove '%c' (%s) from the palette",
                      s.symbol, s.label.empty() ? "no label"
                                                : s.label.c_str());
        DrawMicroText(r, line, {p.x + 24.0f, p.y + 64.0f},
                      14.0f, kStatusText);
        char count[80];
        std::snprintf(count, sizeof(count),
                      "and erase %d marker%s from the map.",
                      n, n == 1 ? "" : "s");
        DrawMicroText(r, count, {p.x + 24.0f, p.y + 86.0f},
                      14.0f, kStatusText);
        const Color kKeyText(0.85f, 0.90f, 0.98f, 1.0f);
        DrawMicroText(r, "Y / Enter",
                      {p.x + 32.0f, p.y + 140.0f}, 16.0f, kKeyText);
        DrawMicroText(r, "confirm",
                      {p.x + 160.0f, p.y + 140.0f}, 14.0f, kStatusText);
        DrawMicroText(r, "N / Esc",
                      {p.x + 32.0f, p.y + 170.0f}, 16.0f, kKeyText);
        DrawMicroText(r, "cancel",
                      {p.x + 160.0f, p.y + 170.0f}, 14.0f, kStatusText);
    }

    // Add-symbol modal: char row (typed), 8-colour swatch row (clicked)
    // with a HEX text field for arbitrary colours, label row (typed).
    // Click on the hex / label field to switch focus; the active field's
    // outline brightens to kSelectHi and a "_" cursor marks the caret.
    void DrawAddSymbolModal(Renderer2D& r) {
        const AddModalGeo g = ComputeAddModalGeo();
        const Vec2 p = g.panel;
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH},
                   Color(0.0f, 0.0f, 0.0f, 0.70f));
        r.DrawQuad(p, {g.panelW, g.panelH}, kPaneBg);
        DrawRectOutline(r, p, {g.panelW, g.panelH}, 2.0f, kSelectHi);
        DrawMicroText(r, "Add new symbol",
                      {p.x + 24.0f, p.y + 18.0f}, 20.0f, kSelectHi);

        const Color kKeyText(0.85f, 0.90f, 0.98f, 1.0f);
        const Color kFieldBg(0.16f, 0.18f, 0.24f, 1.0f);
        const Color kFieldEdge(0.0f, 0.0f, 0.0f, 0.60f);

        // Symbol row.
        DrawMicroText(r, "Symbol",
                      {p.x + 24.0f, p.y + 64.0f}, 14.0f, kStatusText);
        const Vec2 boxP{p.x + 110.0f, p.y + 56.0f};
        const Vec2 boxS{40.0f, 32.0f};
        r.DrawQuad(boxP, boxS, kFieldBg);
        DrawRectOutline(r, boxP, boxS, 1.0f, kFieldEdge);
        if (m_addChar != 0) {
            const char txt[2] = { m_addChar, 0 };
            const float w = MeasureMicroText(txt, 20.0f);
            DrawMicroText(r, txt,
                          {boxP.x + (boxS.x - w) * 0.5f,
                           boxP.y + (boxS.y - 20.0f) * 0.5f},
                          20.0f, ColorFromHex(m_addColor));
        }
        DrawMicroText(r,
            m_addChar == 0 ? "press any key" : "Backspace to retype",
            {boxP.x + boxS.x + 12.0f, p.y + 64.0f},
            12.0f, kStatusText);

        // Color row -- 8 swatches.
        DrawMicroText(r, "Color",
                      {p.x + 24.0f, p.y + 116.0f}, 14.0f, kStatusText);
        for (int i = 0; i < 8; ++i) {
            const float sx = g.swatchStartX +
                             float(i) * (g.swatchSize + g.swatchGap);
            r.DrawQuad({sx, g.swatchRowY},
                       {g.swatchSize, g.swatchSize},
                       ColorFromHex(kObjAddSwatches[i]));
            const bool active = (m_addColor == kObjAddSwatches[i]);
            DrawRectOutline(r, {sx, g.swatchRowY},
                            {g.swatchSize, g.swatchSize},
                            active ? 2.0f : 1.0f,
                            active ? kSelectHi : kFieldEdge);
        }

        // Hex row.
        DrawMicroText(r, "Hex",
                      {p.x + 24.0f, p.y + 194.0f}, 14.0f, kStatusText);
        r.DrawQuad(g.hexP, g.hexS, kFieldBg);
        const bool hexFocus = (m_addFocus == 1);
        DrawRectOutline(r, g.hexP, g.hexS,
                        hexFocus ? 2.0f : 1.0f,
                        hexFocus ? kSelectHi : kFieldEdge);
        // Live colour preview swatch right of the hex box, so the user
        // can see the typed colour without scanning the symbol box.
        const Vec2 prevP{g.hexP.x + g.hexS.x + 12.0f, g.hexP.y};
        const Vec2 prevS{32.0f, g.hexS.y};
        r.DrawQuad(prevP, prevS, ColorFromHex(m_addColor));
        DrawRectOutline(r, prevP, prevS, 1.0f, kFieldEdge);
        const std::string hexShown =
            std::string("#") + m_addHex + (hexFocus ? "_" : "");
        DrawMicroText(r, hexShown,
                      {g.hexP.x + 8.0f,
                       g.hexP.y + (g.hexS.y - 14.0f) * 0.5f},
                      14.0f, kStatusText);

        // Label row.
        DrawMicroText(r, "Label",
                      {p.x + 24.0f, p.y + 250.0f}, 14.0f, kStatusText);
        const bool lblFocus = (m_addFocus == 0);
        r.DrawQuad(g.lblP, g.lblS, kFieldBg);
        DrawRectOutline(r, g.lblP, g.lblS,
                        lblFocus ? 2.0f : 1.0f,
                        lblFocus ? kSelectHi : kFieldEdge);
        const std::string lblShown =
            m_addLabel + (lblFocus ? "_" : "");
        DrawMicroText(r, lblShown,
                      {g.lblP.x + 8.0f,
                       g.lblP.y + (g.lblS.y - 14.0f) * 0.5f},
                      14.0f, kStatusText);

        // Footer hint.
        DrawMicroText(r,
            m_addChar == 0
                ? "Type the symbol character first, then click a field."
                : "Enter: confirm    Esc: cancel    "
                  "click field to switch label / hex",
            {p.x + 24.0f, p.y + g.panelH - 28.0f}, 12.0f, kStatusText);
    }

    // Unsaved-changes confirmation. Triggered by Esc when m_dirty is
    // true; the OnUpdate modal block routes S / D / C keys to save+quit,
    // discard+quit, or cancel respectively.
    void DrawQuitConfirm(Renderer2D& r) {
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH},
                   Color(0.0f, 0.0f, 0.0f, 0.70f));
        constexpr float panelW = 520.0f;
        constexpr float panelH = 200.0f;
        const Vec2 p{(kScreenW - panelW) * 0.5f,
                     (kScreenH - panelH) * 0.5f};
        r.DrawQuad(p, {panelW, panelH}, kPaneBg);
        DrawRectOutline(r, p, {panelW, panelH}, 2.0f, kSelectHi);
        DrawMicroText(r, "Unsaved changes",
                      {p.x + 24.0f, p.y + 22.0f}, 22.0f, kSelectHi);
        DrawMicroText(r,
            "Save before quitting? Your work is not yet on disk.",
            {p.x + 24.0f, p.y + 62.0f}, 14.0f, kStatusText);
        // Key rows. Keep colours consistent with the F1 cheatsheet so the
        // user reads the same "key = light-blue, action = grey" mapping.
        const Color kKeyText(0.85f, 0.90f, 0.98f, 1.0f);
        const float rowY = p.y + 110.0f;
        DrawMicroText(r, "S",        {p.x + 32.0f,  rowY}, 16.0f, kKeyText);
        DrawMicroText(r, "save then quit",
                      {p.x + 64.0f,  rowY}, 14.0f, kStatusText);
        DrawMicroText(r, "D",        {p.x + 32.0f,  rowY + 28.0f}, 16.0f,
                      kKeyText);
        DrawMicroText(r, "discard and quit",
                      {p.x + 64.0f,  rowY + 28.0f}, 14.0f, kStatusText);
        DrawMicroText(r, "C / Esc",  {p.x + 280.0f, rowY}, 16.0f, kKeyText);
        DrawMicroText(r, "stay in editor",
                      {p.x + 360.0f, rowY}, 14.0f, kStatusText);
    }

    // Marker-metadata modal. Single text input bound to m_metaModalBuf;
    // Enter / Esc handled by the OnUpdate modal block. The marker is
    // identified by m_metaModalIdx into m_objects.
    void DrawMetadataModal(Renderer2D& r) {
        if (size_t(m_metaModalIdx) >= m_objects.size()) return;
        const auto& mref = m_objects[size_t(m_metaModalIdx)];
        r.DrawQuad({0.0f, 0.0f}, {kScreenW, kScreenH},
                   Color(0.0f, 0.0f, 0.0f, 0.70f));
        constexpr float panelW = 640.0f;
        constexpr float panelH = 200.0f;
        const Vec2 p{(kScreenW - panelW) * 0.5f,
                     (kScreenH - panelH) * 0.5f};
        r.DrawQuad(p, {panelW, panelH}, kPaneBg);
        DrawRectOutline(r, p, {panelW, panelH}, 2.0f, kSelectHi);

        char title[96];
        std::snprintf(title, sizeof(title),
                      "Marker metadata: '%c' at (%d, %d)",
                      mref.symbol, mref.x, mref.y);
        DrawMicroText(r, title,
                      {p.x + 24.0f, p.y + 22.0f}, 20.0f, kSelectHi);

        const Color kFieldBg(0.16f, 0.18f, 0.24f, 1.0f);
        const Color kFieldEdge(0.0f, 0.0f, 0.0f, 0.60f);
        const Vec2 fieldP{p.x + 24.0f, p.y + 80.0f};
        const Vec2 fieldS{panelW - 48.0f, 36.0f};
        r.DrawQuad(fieldP, fieldS, kFieldBg);
        DrawRectOutline(r, fieldP, fieldS, 2.0f, kSelectHi);
        // The trailing "_" caret is drawn at the end of the buffer; long
        // strings scroll right as they exceed the field width, but for
        // 256 chars / typical metadata that rarely happens.
        const std::string shown = m_metaModalBuf + "_";
        DrawMicroText(r, shown,
                      {fieldP.x + 8.0f,
                       fieldP.y + (fieldS.y - 14.0f) * 0.5f},
                      14.0f, kStatusText);

        DrawMicroText(r,
            "Free-form text -- the game parses it (e.g. "
            "'to=room2,side=west' on doors).",
            {p.x + 24.0f, p.y + 130.0f}, 12.0f, kStatusText);
        DrawMicroText(r, "Enter: save    Esc: cancel    Backspace to edit",
                      {p.x + 24.0f, p.y + panelH - 28.0f},
                      12.0f, kStatusText);
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
        char selBuf[32];
        if (m_mode == EditLayer::Tile && (m_stampW > 1 || m_stampH > 1)) {
            std::snprintf(selBuf, sizeof(selBuf), "%d (%dx%d)",
                          selVal, m_stampW, m_stampH);
        } else {
            std::snprintf(selBuf, sizeof(selBuf), "%d", selVal);
        }
        char brushBuf[16];
        const int effBrush = EffectiveBrushSize();
        if (effBrush > 1)
            std::snprintf(brushBuf, sizeof(brushBuf), "BRUSH: %dx%d   ",
                          effBrush, effBrush);
        else
            brushBuf[0] = '\0';
        char autoBuf[24];
        if (m_autoTile)
            std::snprintf(autoBuf, sizeof(autoBuf),
                          "AUTO: %d..%d   ",
                          m_autoTileBase,
                          m_autoTileBase + kAutoTileCount - 1);
        else
            autoBuf[0] = '\0';
        std::snprintf(buf, sizeof(buf),
                      "MAP: %dx%d   TILE: %dx%d   ZOOM: %.0f%%   "
                      "MODE: %s   SEL: %s   %s%sHOVER: %d,%d   "
                      "UNDO: %zu/%zu   %s   "
                      "F1 help",
                      m_map.Width(), m_map.Height(), m_tileW, m_tileH,
                      m_zoom * 100.0f, modeName, selBuf, brushBuf,
                      autoBuf,
                      hx, hy, m_undoStack.size(), m_redoStack.size(),
                      m_dirty ? "MODIFIED" : "SAVED");
        DrawMicroText(r, buf, {12.0f, y + 8.0f}, 12.0f, kStatusText);
    }

    // ----- State -----
    Renderer2D                  m_renderer;
    MSDFFont                    m_font;
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
    // Per-overlay visibility for the non-active edit layers. Tile is the
    // always-on base layer (collision and objects would have no visual
    // context without it), so only collision tint and object markers
    // have toggles. H flips both together; Shift+1 / Shift+2 isolate
    // one overlay so the user can stare at, say, the collision grid
    // without losing the tile reference.
    bool  m_showCollision = true;
    bool  m_showObjects   = true;

    Vec2  m_panAnchor{0.0f, 0.0f};
    Vec2  m_camAnchor{0.0f, 0.0f};
    bool  m_panning = false;

    // Scrollbar thumb drag. 0 = idle, 1 = horizontal thumb, 2 = vertical
    // thumb. ScrollGeo's CamMin/Max bounds and the (camRange/trackRange)
    // scale are snapshotted at drag start so the drag math stays linear
    // while m_camera moves -- otherwise the live ScrollGeo recomputes its
    // extent from the camera each frame and the thumb drifts under the
    // cursor.
    int   m_scrollDrag         = 0;
    float m_scrollAnchorMouse  = 0.0f;
    float m_scrollAnchorCam    = 0.0f;
    float m_scrollAnchorCamMin = 0.0f;
    float m_scrollAnchorCamMax = 0.0f;
    float m_scrollAnchorScale  = 0.0f;

    // F1 keybind cheatsheet. Modal: all other input is suppressed while
    // the overlay is up so the user can read without accidentally editing.
    bool  m_helpOverlay = false;

    // Esc-with-unsaved confirmation. Modal until S/D/C/Esc resolves it.
    bool  m_quitConfirm = false;

    // Edge-handle map resize. m_resizeHoverEdge highlights the edge under
    // the cursor (so the user sees the affordance before clicking); a
    // press on it latches m_resizingMap, and release commits via
    // DoResize. m_resizeMouseOrigin captures the cursor position at the
    // press so ComputeResizeDelta() can derive the row/col delta.
    bool       m_resizingMap     = false;
    ResizeEdge m_resizingEdge    = ResizeEdge::None;
    ResizeEdge m_resizeHoverEdge = ResizeEdge::None;
    Vec2       m_resizeMouseOrigin{0.0f, 0.0f};

    // Minimap click / drag to jump the camera. The drag flag also gates
    // canvas paint / edge resize / scrollbar drag so a press in the
    // minimap can't bleed into another action while held.
    bool       m_minimapDrag = false;

    // Tileset PNG hot reload. Watches the directory the current tileset
    // sits in and queues a reload whenever the leaf filename modifies
    // -- so editing the PNG in an external tool (Aseprite, Photoshop,
    // GIMP) refreshes the editor's swatches + canvas + minimap on the
    // next frame without needing to reopen the map.
    AssetWatcher m_tilesetWatcher;
    bool         m_tilesetReloadPending = false;
    std::string  m_currentTilesetAbsPath;

    // Recent files (most-recent first), persisted to %APPDATA%/NaraGame/
    // editor-recent.cfg. Ctrl+R opens the overlay; 1..N pick an entry.
    static constexpr int kMaxRecent = 5;
    std::vector<std::string> m_recent;
    bool                     m_recentOverlay = false;

    // Periodic safety-net: while the map is dirty, every kBackupInterval
    // seconds the editor writes a sibling "<path>.bak" copy. The user's
    // m_dirty state is untouched -- this is purely a side-write so a
    // crash mid-edit doesn't lose work since the last explicit Ctrl+S.
    static constexpr float kBackupInterval = 300.0f;   // 5 minutes
    float m_backupTimer = 0.0f;

    // Auto-tile (T to toggle). 47-variant terrain: tiles
    // [m_autoTileBase, m_autoTileBase + kAutoTileCount) form a single
    // terrain in canonical 8-bit mask order. The mask layout follows
    // the standard "blob47" convention used by RPG Maker / Godot:
    //   bit 0 = N  (cardinal)
    //   bit 1 = NE (corner; suppressed unless N AND E are both same)
    //   bit 2 = E  (cardinal)
    //   bit 3 = SE (corner; suppressed unless S AND E both same)
    //   bit 4 = S  (cardinal)
    //   bit 5 = SW (corner; suppressed unless S AND W both same)
    //   bit 6 = W  (cardinal)
    //   bit 7 = NW (corner; suppressed unless N AND W both same)
    // Of the 2^8=256 raw masks only 47 are canonical (corners gated by
    // their cardinals); the variant index is the rank of the canonical
    // mask in numeric order, computed once into kAutoMaskToVariant at
    // first use. LMB paint places base+variant at the hover cell and
    // refreshes the variant of every cell in the 3x3 neighbourhood
    // that already belongs to the terrain. RMB clears + refreshes.
    // Brush size and stamps are bypassed while auto-tile mode is on.
    static constexpr int kAutoTileCount = 47;
    bool m_autoTile     = false;
    int  m_autoTileBase = 0;
    // Registered terrains, persisted to the .ntmap autotile section.
    // T captures the current selectedTile as a new terrain if no
    // existing one contains it, otherwise activates the matching one.
    std::vector<AutoTileTerrain> m_autotiles;

    static const int* AutoMaskToVariantTable() {
        static int table[256];
        static bool init = false;
        if (!init) {
            init = true;
            int idx = 0;
            for (int m = 0; m < 256; ++m) {
                const bool ne = (m & 0x02) != 0;
                const bool se = (m & 0x08) != 0;
                const bool sw = (m & 0x20) != 0;
                const bool nw = (m & 0x80) != 0;
                const bool n  = (m & 0x01) != 0;
                const bool e  = (m & 0x04) != 0;
                const bool s  = (m & 0x10) != 0;
                const bool w  = (m & 0x40) != 0;
                if (ne && !(n && e)) { table[m] = -1; continue; }
                if (se && !(s && e)) { table[m] = -1; continue; }
                if (sw && !(s && w)) { table[m] = -1; continue; }
                if (nw && !(n && w)) { table[m] = -1; continue; }
                table[m] = idx++;
            }
            // sanity: should land on 47
        }
        return table;
    }

    // Object-mode marker drag. LMB press on a cell that already has a
    // marker records m_objDragSym and defers any state change to release.
    // If the cursor leaves the source cell before release (m_objDragging
    // turns true), the release moves the marker to the new cell. A press
    // that releases without ever moving falls back to the legacy replace
    // behaviour (paint m_selectedSym over the source cell). Press on an
    // empty cell -- and Tile / Collision modes -- skip this whole path
    // and continuous-paint as before.
    char m_objDragSym    = 0;
    int  m_objDragSrcTx  = 0;
    int  m_objDragSrcTy  = 0;
    bool m_objDragging   = false;

    // Object-palette symbol management. Both modals are modal: when up,
    // OnUpdate returns early after running their own input so no canvas
    // / palette / scrollbar action fires under them.
    int         m_deleteSymbolIdx = -1;    // >=0 = index awaiting confirm
    bool        m_addSymbolModal  = false; // panel for char/color/label
    char        m_addChar         = 0;     // 0 = not yet typed
    uint32_t    m_addColor        = 0xCCCCCCu;
    std::string m_addLabel;
    // HEX text input alongside the swatch row, for arbitrary colours
    // outside the eight presets. Tracked separately so clicking a
    // swatch refreshes the field to that swatch's hex; typing six valid
    // hex chars updates m_addColor live.
    std::string m_addHex;
    int         m_addFocus = 0;            // 0 = label, 1 = hex

    // Marker metadata modal. RMB on an existing marker in Object mode
    // opens this for editing the marker's optional free-form data
    // string (e.g. "to=room2,side=west" on door markers). Enter commits,
    // Esc cancels.
    int         m_metaModalIdx = -1;       // m_objects index; -1 = closed
    std::string m_metaModalBuf;            // edit buffer

    // Brush size for single-value paint (Tile single-tile, Collision).
    // Always odd so the brush footprint is centred on the hover cell.
    // Object mode forces 1 via EffectiveBrushSize().
    int   m_brushSize = 1;

    float m_paletteScroll = 0.0f;
    int   m_selectedTile = 0;

    // Stamp brush. m_selectedTile is the stamp's top-left palette index;
    // m_stampW/H + m_stampTiles describe the WxH block sampled from the
    // palette (row-major). A drag in the palette extends the rectangle;
    // single-click / eyedropper / 1-9 reset to 1x1. -1 in m_stampTiles
    // means "skip this cell" (the rect overhangs the end of the tileset).
    int               m_stampW = 1;
    int               m_stampH = 1;
    std::vector<int>  m_stampTiles = {0};
    bool              m_palDragging = false;
    int               m_palAnchorX = 0;
    int               m_palAnchorY = 0;
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
    // Object-symbol palette. Initialised to DefaultObjectSymbols() so the
    // editor has a usable list on first launch; OpenPath() replaces it when
    // the loaded .ntmap carries its own `symbols` section, and SaveCurrent()
    // persists whatever the list currently holds.
    std::vector<SymbolDef>    m_symbols      = DefaultObjectSymbols();
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
