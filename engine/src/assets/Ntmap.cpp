#include "NaraGame/assets/TilemapLoader.h"

#include "NaraGame/assets/TextureLoader.h"
#include "NaraGame/core/Log.h"
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
    std::vector<int>          tiles;
    std::vector<uint8_t>      collision;
    std::vector<ObjectMarker> objects;
    std::vector<SymbolDef>    symbols;
    bool sawHeader  = false;
    bool sawTileset = false;
    bool sawSize    = false;
    // section tracks which body block subsequent body lines belong to.
    // 0 = none, 1 = layer (tile ids), 2 = collision (uint8 codes),
    // 3 = objects (symbol + x + y per line),
    // 4 = symbols (symbol + hex-rgb + label words per line).
    int section = 0;

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
            // Each line: <symbol> <x> <y>. Skip if the leading token isn't
            // a single printable char (probably a new directive).
            std::string sym;
            int ox, oy;
            if ((ls >> sym >> ox >> oy) && sym.size() == 1) {
                ObjectMarker m;
                m.symbol = sym[0];
                m.x = ox;
                m.y = oy;
                objects.push_back(m);
                continue;
            }
            // Fall through to the directive branch -- this wasn't an object
            // line after all.
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
        if (section != 0 && section != 3 && isData) {
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
            section = 1;
        } else if (tok == "collision") {
            section = 2;
        } else if (tok == "objects") {
            section = 3;
        } else if (tok == "symbols") {
            section = 4;
        }
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
    const size_t expected = size_t(mapW) * size_t(mapH);
    if (tiles.size() < expected) {
        NARAGAME_ERROR("LoadNtmap: '%s' truncated layer (%zu/%zu)",
                       path.c_str(), tiles.size(), expected);
        return asset;
    }

    const std::string absTileset = Resolve(DirOf(path), tilesetRel);
    asset.texture = LoadTexture(device, absTileset, filter);
    if (!asset.texture) {
        NARAGAME_ERROR("LoadNtmap: failed to load tileset '%s'", absTileset.c_str());
        return asset;
    }

    asset.map = Tilemap(mapW, mapH, float(cellW), float(cellH));
    SpriteSheet sheet(asset.texture.get(), cellW, cellH, margin, spacing);
    asset.map.SetTileset(sheet);
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            asset.map.SetTile(x, y, tiles[size_t(y) * mapW + x]);
        }
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
    asset.symbols = std::move(symbols);

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
               const std::vector<SymbolDef>& symbols) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        NARAGAME_ERROR("SaveNtmap: cannot open '%s' for write", path.c_str());
        return false;
    }
    out << "ntmap 1\n";
    out << "tileset " << tilesetPath << ' ' << tileWidth << ' ' << tileHeight
        << ' ' << margin << ' ' << spacing << '\n';
    out << "size " << map.Width() << ' ' << map.Height() << '\n';
    out << "layer\n";
    for (int y = 0; y < map.Height(); ++y) {
        for (int x = 0; x < map.Width(); ++x) {
            if (x) out << ' ';
            out << map.GetTile(x, y);
        }
        out << '\n';
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
            out << m.symbol << ' ' << m.x << ' ' << m.y << '\n';
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
    return out.good();
}

} // namespace NaraGame
