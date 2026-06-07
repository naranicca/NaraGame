#include "NaraGame/assets/TilemapLoader.h"

#include "NaraGame/assets/TextureLoader.h"
#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Tiny Tiled .tmx (XML) reader. The full TMX spec is huge -- this only does
// what the MVP cares about:
//
//   <map width=".." height=".." tilewidth=".." tileheight="..">
//     <tileset firstgid="1" name=".." tilewidth=".." tileheight=".."
//              margin=".." spacing="..">
//       <image source="..." width=".." height=".."/>
//     </tileset>
//     <layer ...>
//       <data encoding="csv">12,34,0,...</data>
//     </layer>
//   </map>
//
// External .tsx tilesets, base64/zlib encoded layers, multi-tileset maps,
// flip/rotation bits in the GID, and object/group/image layers are all
// out of scope -- returns valid=false if any are encountered.

namespace NaraGame {

namespace {

std::string DirOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return {};
    return path.substr(0, slash + 1);
}

std::string Resolve(const std::string& baseDir, const std::string& rel) {
    if (rel.size() >= 2 && rel[1] == ':') return rel;
    if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) return rel;
    return baseDir + rel;
}

// Pull an attribute value out of an opening tag like `<tileset firstgid="1"
// tilewidth="32">`. Returns "" if the attribute isn't there. Quote-only --
// Tiled always quotes attributes, so we don't bother handling apos.
std::string Attr(const std::string& tag, const char* name) {
    std::string needle = std::string(name) + "=\"";
    const size_t pos = tag.find(needle);
    if (pos == std::string::npos) return {};
    const size_t start = pos + needle.size();
    const size_t end = tag.find('"', start);
    if (end == std::string::npos) return {};
    return tag.substr(start, end - start);
}

int AttrInt(const std::string& tag, const char* name, int dflt = 0) {
    const std::string s = Attr(tag, name);
    if (s.empty()) return dflt;
    return std::atoi(s.c_str());
}

// Read `path` into a single std::string. The whole file -- tilemaps are small
// (a few KB to tens of KB), so streaming wouldn't buy us anything.
bool SlurpFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

TilemapAsset LoadTmx(RHIDevice& device, const std::string& path,
                     TextureFilter filter) {
    TilemapAsset asset;

    std::string xml;
    if (!SlurpFile(path, xml)) {
        NARAGAME_ERROR("LoadTmx: cannot open '%s'", path.c_str());
        return asset;
    }

    // <map ...>
    const size_t mapOpen = xml.find("<map");
    if (mapOpen == std::string::npos) {
        NARAGAME_ERROR("LoadTmx: '%s' has no <map>", path.c_str());
        return asset;
    }
    const size_t mapTagEnd = xml.find('>', mapOpen);
    const std::string mapTag = xml.substr(mapOpen, mapTagEnd - mapOpen + 1);
    const int mapW    = AttrInt(mapTag, "width");
    const int mapH    = AttrInt(mapTag, "height");
    const int tileW   = AttrInt(mapTag, "tilewidth");
    const int tileH   = AttrInt(mapTag, "tileheight");
    if (mapW <= 0 || mapH <= 0 || tileW <= 0 || tileH <= 0) {
        NARAGAME_ERROR("LoadTmx: '%s' has bad <map> dimensions", path.c_str());
        return asset;
    }

    // <tileset ...> -- accept only inline (with <image> child), single tileset.
    const size_t tsOpen = xml.find("<tileset", mapTagEnd);
    if (tsOpen == std::string::npos) {
        NARAGAME_ERROR("LoadTmx: '%s' has no <tileset>", path.c_str());
        return asset;
    }
    const size_t tsTagEnd = xml.find('>', tsOpen);
    const std::string tsTag = xml.substr(tsOpen, tsTagEnd - tsOpen + 1);
    if (!Attr(tsTag, "source").empty()) {
        NARAGAME_ERROR("LoadTmx: '%s' uses external .tsx (unsupported in MVP)",
                       path.c_str());
        return asset;
    }
    const int firstGid = AttrInt(tsTag, "firstgid", 1);
    const int cellW    = AttrInt(tsTag, "tilewidth",  tileW);
    const int cellH    = AttrInt(tsTag, "tileheight", tileH);
    const int margin   = AttrInt(tsTag, "margin",  0);
    const int spacing  = AttrInt(tsTag, "spacing", 0);

    const size_t imgOpen = xml.find("<image", tsTagEnd);
    if (imgOpen == std::string::npos) {
        NARAGAME_ERROR("LoadTmx: '%s' tileset has no <image>", path.c_str());
        return asset;
    }
    const size_t imgTagEnd = xml.find('>', imgOpen);
    const std::string imgTag = xml.substr(imgOpen, imgTagEnd - imgOpen + 1);
    const std::string imgSrc = Attr(imgTag, "source");
    if (imgSrc.empty()) {
        NARAGAME_ERROR("LoadTmx: '%s' tileset image has no source", path.c_str());
        return asset;
    }

    // <layer> ... <data encoding="csv">...</data> ... </layer>
    const size_t layerOpen = xml.find("<layer", imgTagEnd);
    if (layerOpen == std::string::npos) {
        NARAGAME_ERROR("LoadTmx: '%s' has no <layer>", path.c_str());
        return asset;
    }
    const size_t dataOpen = xml.find("<data", layerOpen);
    if (dataOpen == std::string::npos) {
        NARAGAME_ERROR("LoadTmx: '%s' layer has no <data>", path.c_str());
        return asset;
    }
    const size_t dataTagEnd = xml.find('>', dataOpen);
    const std::string dataTag = xml.substr(dataOpen, dataTagEnd - dataOpen + 1);
    const std::string enc = Attr(dataTag, "encoding");
    if (enc != "csv") {
        NARAGAME_ERROR("LoadTmx: '%s' uses encoding '%s' (only csv supported)",
                       path.c_str(), enc.c_str());
        return asset;
    }
    const size_t dataClose = xml.find("</data>", dataTagEnd);
    if (dataClose == std::string::npos) {
        NARAGAME_ERROR("LoadTmx: '%s' data section is unterminated", path.c_str());
        return asset;
    }
    const std::string body = xml.substr(dataTagEnd + 1, dataClose - dataTagEnd - 1);

    // Parse CSV. Tiled writes one row per line, comma-separated. We're tolerant
    // about trailing commas and whitespace.
    std::vector<int> gids;
    gids.reserve(size_t(mapW) * size_t(mapH));
    {
        std::string tok;
        for (char c : body) {
            if (c == ',' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
                if (!tok.empty()) {
                    gids.push_back(std::atoi(tok.c_str()));
                    tok.clear();
                }
            } else {
                tok.push_back(c);
            }
        }
        if (!tok.empty()) gids.push_back(std::atoi(tok.c_str()));
    }
    const size_t expected = size_t(mapW) * size_t(mapH);
    if (gids.size() < expected) {
        NARAGAME_ERROR("LoadTmx: '%s' csv truncated (%zu/%zu)", path.c_str(),
                       gids.size(), expected);
        return asset;
    }
    // Reject any GID with the rotation/flip bits set -- the top three bits
    // are used by Tiled to flag horizontal/vertical/diagonal flips and the
    // MVP doesn't honour them, so silently using them would draw wrong.
    constexpr uint32_t kFlipMask = 0xE0000000u;
    for (size_t i = 0; i < expected; ++i) {
        if (uint32_t(gids[i]) & kFlipMask) {
            NARAGAME_ERROR("LoadTmx: '%s' uses flip/rotation bits (unsupported)",
                           path.c_str());
            return asset;
        }
    }

    const std::string absTileset = Resolve(DirOf(path), imgSrc);
    asset.texture = LoadTexture(device, absTileset, filter);
    if (!asset.texture) {
        NARAGAME_ERROR("LoadTmx: failed to load tileset '%s'", absTileset.c_str());
        return asset;
    }

    asset.map = Tilemap(mapW, mapH, float(cellW), float(cellH));
    SpriteSheet sheet(asset.texture.get(), cellW, cellH, margin, spacing);
    asset.map.SetTileset(sheet);
    // Tiled GIDs: 0 = empty; otherwise tile index = gid - firstgid.
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            const int g = gids[size_t(y) * mapW + x];
            const int id = (g == 0) ? -1 : (g - firstGid);
            asset.map.SetTile(x, y, id);
        }
    }
    asset.tilesetPath = imgSrc;
    asset.tileWidth   = cellW;
    asset.tileHeight  = cellH;
    asset.valid       = true;
    return asset;
}

} // namespace NaraGame
