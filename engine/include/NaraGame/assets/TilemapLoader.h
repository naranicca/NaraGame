#pragma once

#include "NaraGame/renderer/SpriteSheet.h"
#include "NaraGame/renderer/Tilemap.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace NaraGame {

class RHIDevice;
class RHITexture;

// Engine-wide collision codes baked into a TilemapAsset's optional
// collision grid. Stored as uint8_t (one byte per cell, row-major). 0
// matches `Tile::Floor` in games that use the brawl-style grid; the
// editor authors them as separate paint kinds.
namespace Collision {
    constexpr uint8_t Floor = 0;
    constexpr uint8_t Wall  = 1;
    constexpr uint8_t Water = 2;
}

// Object marker — a single character at a tile coordinate, written to the
// .ntmap's optional `objects` section. Meaning is up to the game: brawl
// reads `P` as the player spawn, `E` as an enemy spawn, etc.
struct ObjectMarker {
    char symbol = 0;
    int  x = 0;
    int  y = 0;
};

// Per-character metadata for the `objects` palette: human-readable label and
// a colour the editor tints the palette cell with. Lives in an optional
// `symbols` section in the .ntmap so each map can define its own marker set
// (mario M/g/!/* vs brawl P/E/I/C/X) without recompiling the editor. Games
// ignore this section entirely -- they only consume the bare `symbol` char.
struct SymbolDef {
    char        symbol = 0;
    uint32_t    color  = 0xFFFFFFu;   // 0xRRGGBB; alpha implied opaque
    std::string label;
};

// Bundle a Tilemap + the texture its SpriteSheet samples from. The texture
// lives inside the asset because the SpriteSheet inside `map.SetTileset(...)`
// only stores a raw pointer -- if the texture goes out of scope the tilemap's
// regions become dangling. Keep the asset alive for as long as the map is
// drawn.
//
// `collision` is an optional same-dimensioned grid of Collision::* codes.
// Empty if the .ntmap had no `collision` section; the loader does NOT
// synthesize one (so a game can tell "all floor" from "absent" if it cares).
struct TilemapAsset {
    std::unique_ptr<RHITexture> texture;
    Tilemap                     map;
    std::vector<uint8_t>        collision;     // mapW * mapH; empty if absent
    std::vector<ObjectMarker>   objects;       // empty if absent
    std::vector<SymbolDef>      symbols;       // empty if absent
    std::string                 tilesetPath;   // relative path saved in the file
    int                         tileWidth  = 0;
    int                         tileHeight = 0;
    bool                        valid      = false;
};

// .ntmap is a tiny plain-text format authored by the in-engine editor:
//
//   ntmap 1
//   tileset <path> <cellW> <cellH> <margin> <spacing>
//   size <mapW> <mapH>
//   layer
//   <id> <id> ... <id>       # mapW ints, repeated mapH times; -1 = empty
//
// `path` is resolved relative to the .ntmap file's own directory. Returns an
// asset with valid=false if parsing fails or the texture can't be loaded.
TilemapAsset LoadNtmap(RHIDevice& device, const std::string& path,
                       TextureFilter filter = TextureFilter::Point);

// Tiled .tmx (XML) with a single inline tileset and a single CSV-encoded
// layer. Rotation/flip flags, external TSX, base64/zlib are intentionally
// out of scope for the MVP -- returns valid=false if the file uses any.
TilemapAsset LoadTmx(RHIDevice& device, const std::string& path,
                     TextureFilter filter = TextureFilter::Point);

// Write a Tilemap back out in .ntmap format. `tilesetPath` is stored verbatim,
// so callers should pass either an absolute path or one resolved relative to
// the .ntmap file's directory. If `collision` is non-empty and matches the
// map dimensions, a `collision` section is appended; pass an empty vector
// to skip it. `objects` is appended as a list when non-empty. Returns false
// on I/O failure.
bool SaveNtmap(const std::string& path, const Tilemap& map,
               const std::string& tilesetPath, int tileWidth, int tileHeight,
               int margin = 0, int spacing = 0,
               const std::vector<uint8_t>& collision = {},
               const std::vector<ObjectMarker>& objects = {},
               const std::vector<SymbolDef>& symbols = {});

} // namespace NaraGame
