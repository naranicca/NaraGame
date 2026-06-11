#include "NaraGame/assets/TilemapLoader.h"

#include "NaraGame/assets/TextureLoader.h"
#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/Renderer2D.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace NaraGame {

namespace {

// Slice `path` into its directory component (everything up to and including
// the last slash or backslash). Returns "" if the path has no separator.
std::string DirOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return {};
    return path.substr(0, slash + 1);
}

// Resolve `rel` against `baseDir`. Absolute paths (Windows drive or leading
// slash) pass through; everything else is joined.
std::string Resolve(const std::string& baseDir, const std::string& rel) {
    if (rel.size() >= 2 && rel[1] == ':') return rel;           // C:\...
    if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) return rel;
    return baseDir + rel;
}

} // namespace

TilemapAsset LoadNtmap(RHIDevice& device, const std::string& path,
                       TextureFilter filter) {
    TilemapAsset asset;
    std::ifstream in(path);
    if (!in.is_open()) {
        NARAGAME_ERROR("LoadNtmap: cannot open '%s'", path.c_str());
        return asset;
    }

    std::string tilesetRel;
    int cellW = 0, cellH = 0, margin = 0, spacing = 0;
    int mapW = 0, mapH = 0;
    // One `layer` directive starts a fresh tile vector; the previous one
    // (if any) gets pushed into `allLayers`. After the file is fully
    // parsed, allLayers[0] becomes asset.map and the rest go to
    // asset.extraLayers.
    std::vector<std::vector<int>> allLayers;
    std::vector<int>          tiles;
    std::vector<uint8_t>      collision;
    std::vector<ObjectMarker> objects;
    std::vector<SymbolDef>    symbols;
    std::vector<AutoTileTerrain> autotiles;
    bool sawHeader  = false;
    bool sawTileset = false;
    bool sawSize    = false;
    // section tracks which body block subsequent body lines belong to.
    // 0 = none, 1 = layer (tile ids), 2 = collision (uint8 codes),
    // 3 = objects (symbol + x + y per line),
    // 4 = symbols (symbol + hex-rgb + label words per line),
    // 5 = autotile (base + count + kind per line),
    // 6 = parallax (one float per line; one entry per tile layer).
    int section = 0;
    std::vector<float> parallax;

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        std::istringstream ls(line);
        // A numeric line continues a layer/collision section. The objects
        // section is line-oriented (`<sym> <x> <y>`), so we route it on its
        // own: a line is an object entry as long as the active section is
        // "objects" and we can read at least a symbol character.
        const char first = line[0];
        const bool isData = (first == '-' || (first >= '0' && first <= '9'));
        if (section == 3) {
            // Each line: <symbol> <x> <y> [data...]. Skip if the leading
            // token isn't a single printable char (probably a new directive).
            std::string sym;
            int ox, oy;
            if ((ls >> sym >> ox >> oy) && sym.size() == 1) {
                ObjectMarker m;
                m.symbol = sym[0];
                m.x = ox;
                m.y = oy;
                // Anything after the y coord on the same line is the
                // optional free-form payload. Trim leading/trailing
                // whitespace (and trailing \r from CRLF on Windows).
                std::string rest;
                std::getline(ls, rest);
                const size_t s = rest.find_first_not_of(" \t");
                const size_t e = rest.find_last_not_of(" \t\r\n");
                if (s != std::string::npos) m.data = rest.substr(s, e - s + 1);
                objects.push_back(m);
                continue;
            }
            // Fall through to the directive branch -- this wasn't an object
            // line after all.
            ls.clear();
            ls.seekg(0);
        }
        if (section == 5) {
            // Each line: <base> <count> <kind>. Numeric first token
            // keeps it in the section; anything else is a directive.
            int base = 0, count = 0;
            std::string kind;
            if ((ls >> base >> count >> kind) &&
                base >= 0 && count > 0) {
                AutoTileTerrain t;
                t.base  = base;
                t.count = count;
                t.kind  = kind;
                autotiles.push_back(t);
                continue;
            }
            ls.clear();
            ls.seekg(0);
        }
        if (section == 4) {
            // Each line: <symbol> <hexcolor> <label words...>. Same shape
            // detection as the objects section -- single-char first token
            // means "still in this block", anything else is a new directive.
            std::string sym;
            std::string colorTok;
            if ((ls >> sym >> colorTok) && sym.size() == 1) {
                SymbolDef d;
                d.symbol = sym[0];
                char* endp = nullptr;
                const unsigned long c = std::strtoul(colorTok.c_str(), &endp, 16);
                if (endp == colorTok.c_str() + colorTok.size())
                    d.color = uint32_t(c) & 0xFFFFFFu;
                // Label is the remainder of the line, whitespace-trimmed.
                std::string rest;
                std::getline(ls, rest);
                const size_t s = rest.find_first_not_of(" \t");
                const size_t e = rest.find_last_not_of(" \t\r\n");
                if (s != std::string::npos)
                    d.label = rest.substr(s, e - s + 1);
                symbols.push_back(d);
                continue;
            }
            ls.clear();
            ls.seekg(0);
        }
        if (section == 6) {
            // Each line is a single float. Negative leading char counts
            // as data because the parallax factor may be a fraction like
            // 0.3 or even 0 (foreground that doesn't track the camera at
            // all). isData covers digits + '-', not '.' or '0.x' starts,
            // so handle this section explicitly.
            float f = 0.0f;
            if (ls >> f) {
                parallax.push_back(f);
                continue;
            }
            ls.clear();
            ls.seekg(0);
        }
        if (section != 0 && section != 3 && section != 6 && isData) {
            int v;
            while (ls >> v) {
                if (section == 1) tiles.push_back(v);
                else if (section == 2) collision.push_back(uint8_t(v));
            }
            continue;
        }
        section = 0;
        std::string tok;
        if (!(ls >> tok)) continue;
        if (tok == "ntmap") {
            int ver = 0;
            ls >> ver;
            if (ver != 1) {
                NARAGAME_ERROR("LoadNtmap: unsupported version %d in '%s'", ver,
                               path.c_str());
                return asset;
            }
            sawHeader = true;
        } else if (tok == "tileset") {
            ls >> tilesetRel >> cellW >> cellH;
            if (!(ls >> margin)) margin = 0;
            if (!(ls >> spacing)) spacing = 0;
            sawTileset = true;
        } else if (tok == "size") {
            ls >> mapW >> mapH;
            sawSize = true;
        } else if (tok == "layer") {
            // Starting a new layer: flush whatever ids the previous `layer`
            // section accumulated, then start a fresh tile vector. The
            // first `layer` directive fires this with an empty `tiles`, so
            // we skip the push.
            if (!tiles.empty()) {
                allLayers.push_back(std::move(tiles));
                tiles.clear();
            }
            section = 1;
        } else if (tok == "collision") {
            section = 2;
        } else if (tok == "objects") {
            section = 3;
        } else if (tok == "symbols") {
            section = 4;
        } else if (tok == "autotile") {
            section = 5;
        } else if (tok == "parallax") {
            section = 6;
        }
    }

    // Flush the final layer the parser was still filling.
    if (!tiles.empty()) {
        allLayers.push_back(std::move(tiles));
        tiles.clear();
    }

    if (!sawHeader || !sawTileset || !sawSize) {
        NARAGAME_ERROR("LoadNtmap: '%s' missing required header/tileset/size",
                       path.c_str());
        return asset;
    }
    if (mapW <= 0 || mapH <= 0 || cellW <= 0 || cellH <= 0) {
        NARAGAME_ERROR("LoadNtmap: '%s' has bad dimensions", path.c_str());
        return asset;
    }
    if (allLayers.empty()) {
        NARAGAME_ERROR("LoadNtmap: '%s' has no layer section", path.c_str());
        return asset;
    }
    const size_t expected = size_t(mapW) * size_t(mapH);
    for (size_t i = 0; i < allLayers.size(); ++i) {
        if (allLayers[i].size() < expected) {
            NARAGAME_ERROR("LoadNtmap: '%s' truncated layer %zu (%zu/%zu)",
                           path.c_str(), i, allLayers[i].size(), expected);
            return asset;
        }
    }

    const std::string absTileset = Resolve(DirOf(path), tilesetRel);
    asset.texture = LoadTexture(device, absTileset, filter);
    if (!asset.texture) {
        NARAGAME_ERROR("LoadNtmap: failed to load tileset '%s'", absTileset.c_str());
        return asset;
    }

    SpriteSheet sheet(asset.texture.get(), cellW, cellH, margin, spacing);
    auto buildLayer = [&](const std::vector<int>& src) {
        Tilemap t(mapW, mapH, float(cellW), float(cellH));
        t.SetTileset(sheet);
        for (int y = 0; y < mapH; ++y) {
            for (int x = 0; x < mapW; ++x) {
                t.SetTile(x, y, src[size_t(y) * mapW + x]);
            }
        }
        return t;
    };
    asset.map = buildLayer(allLayers[0]);
    for (size_t i = 1; i < allLayers.size(); ++i) {
        asset.extraLayers.push_back(buildLayer(allLayers[i]));
    }
    // Always emit a parallax vector sized to the layer count so renderers
    // can read [i] without a presence check. Missing entries in the file
    // (or absent section entirely) default to 1.0 = "tracks the camera
    // normally", which matches single-layer behaviour from before.
    asset.layerParallax.assign(allLayers.size(), 1.0f);
    for (size_t i = 0; i < parallax.size() && i < allLayers.size(); ++i) {
        asset.layerParallax[i] = parallax[i];
    }
    // Optional collision: only attached if the file provided exactly the
    // right number of codes. A short / oversized section is treated as an
    // authoring mistake and dropped (the game then falls back to whatever
    // it does with no collision data).
    if (collision.size() == expected) {
        asset.collision = std::move(collision);
    } else if (!collision.empty()) {
        NARAGAME_WARN("LoadNtmap: '%s' collision size %zu != %zu, ignored",
                      path.c_str(), collision.size(), expected);
    }
    // Objects: drop entries pointing outside the map but keep the rest;
    // resizing a map shouldn't silently delete every marker.
    for (const auto& m : objects) {
        if (m.x >= 0 && m.y >= 0 && m.x < mapW && m.y < mapH) {
            asset.objects.push_back(m);
        }
    }
    asset.symbols   = std::move(symbols);
    asset.autotiles = std::move(autotiles);

    asset.tilesetPath = tilesetRel;
    asset.tileWidth   = cellW;
    asset.tileHeight  = cellH;
    asset.valid       = true;
    return asset;
}

bool SaveNtmap(const std::string& path, const Tilemap& map,
               const std::string& tilesetPath, int tileWidth, int tileHeight,
               int margin, int spacing,
               const std::vector<uint8_t>& collision,
               const std::vector<ObjectMarker>& objects,
               const std::vector<SymbolDef>& symbols,
               const std::vector<AutoTileTerrain>& autotiles,
               const std::vector<Tilemap>& extraLayers,
               const std::vector<float>& layerParallax) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        NARAGAME_ERROR("SaveNtmap: cannot open '%s' for write", path.c_str());
        return false;
    }
    out << "ntmap 1\n";
    out << "tileset " << tilesetPath << ' ' << tileWidth << ' ' << tileHeight
        << ' ' << margin << ' ' << spacing << '\n';
    out << "size " << map.Width() << ' ' << map.Height() << '\n';
    auto writeLayer = [&](const Tilemap& t) {
        out << "layer\n";
        for (int y = 0; y < t.Height(); ++y) {
            for (int x = 0; x < t.Width(); ++x) {
                if (x) out << ' ';
                out << t.GetTile(x, y);
            }
            out << '\n';
        }
    };
    writeLayer(map);
    // Extra layers only get written when their dimensions match the base
    // map. A mismatch is an authoring bug we'd rather drop than persist:
    // a partial overlay would round-trip with phantom -1 cells.
    for (const auto& layer : extraLayers) {
        if (layer.Width() != map.Width() || layer.Height() != map.Height()) {
            NARAGAME_WARN(
                "SaveNtmap: extra layer %dx%d != base %dx%d, skipped",
                layer.Width(), layer.Height(), map.Width(), map.Height());
            continue;
        }
        writeLayer(layer);
    }
    // Only emit the collision section when the caller supplied one whose
    // dimensions match the map (otherwise a partial grid would round-trip
    // and silently corrupt the map).
    const size_t expected = size_t(map.Width()) * size_t(map.Height());
    if (collision.size() == expected) {
        out << "collision\n";
        for (int y = 0; y < map.Height(); ++y) {
            for (int x = 0; x < map.Width(); ++x) {
                if (x) out << ' ';
                out << int(collision[size_t(y) * map.Width() + x]);
            }
            out << '\n';
        }
    }
    if (!objects.empty()) {
        out << "objects\n";
        for (const auto& m : objects) {
            // Skip markers that no longer fit. They've been dropped from
            // the asset by the loader's clip step too, so a round-trip
            // never resurrects them.
            if (m.x < 0 || m.y < 0 ||
                m.x >= map.Width() || m.y >= map.Height()) continue;
            out << m.symbol << ' ' << m.x << ' ' << m.y;
            if (!m.data.empty()) out << ' ' << m.data;
            out << '\n';
        }
    }
    if (!symbols.empty()) {
        out << "symbols\n";
        char hex[8];
        for (const auto& s : symbols) {
            if (s.symbol == 0) continue;
            std::snprintf(hex, sizeof(hex), "%06x",
                          unsigned(s.color & 0xFFFFFFu));
            out << s.symbol << ' ' << hex << ' ' << s.label << '\n';
        }
    }
    if (!autotiles.empty()) {
        out << "autotile\n";
        for (const auto& t : autotiles) {
            if (t.count <= 0) continue;
            out << t.base << ' ' << t.count << ' '
                << (t.kind.empty() ? std::string("blob47") : t.kind)
                << '\n';
        }
    }
    // Only emit the parallax section when at least one factor differs
    // from 1.0 -- the default round-trips without writing anything new
    // for legacy single-layer maps that never set it.
    bool anyNonOne = false;
    for (float f : layerParallax) {
        if (f != 1.0f) { anyNonOne = true; break; }
    }
    if (anyNonOne) {
        out << "parallax\n";
        for (float f : layerParallax) out << f << '\n';
    }
    return out.good();
}

void DrawAllLayers(const TilemapAsset& asset, Renderer2D& renderer,
                   const Vec4& visible, const Color& tint) {
    if (!asset.valid) return;
    asset.map.Draw(renderer, visible, tint);
    for (const auto& layer : asset.extraLayers) {
        layer.Draw(renderer, visible, tint);
    }
}

} // namespace NaraGame
