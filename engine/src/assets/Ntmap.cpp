#include "NaraGame/assets/TilemapLoader.h"

#include "NaraGame/assets/TextureLoader.h"
#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"

#include <cstdio>
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
    std::vector<int> tiles;
    bool sawHeader = false;
    bool sawTileset = false;
    bool sawSize = false;
    bool inLayer = false;

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        std::istringstream ls(line);
        if (inLayer) {
            int v;
            while (ls >> v) tiles.push_back(v);
            continue;
        }
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
            inLayer = true;
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
    asset.tilesetPath = tilesetRel;
    asset.tileWidth   = cellW;
    asset.tileHeight  = cellH;
    asset.valid       = true;
    return asset;
}

bool SaveNtmap(const std::string& path, const Tilemap& map,
               const std::string& tilesetPath, int tileWidth, int tileHeight,
               int margin, int spacing) {
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
    return out.good();
}

} // namespace NaraGame
