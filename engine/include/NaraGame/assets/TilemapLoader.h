#pragma once

#include "NaraGame/renderer/SpriteSheet.h"
#include "NaraGame/renderer/Tilemap.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

#include <memory>
#include <string>

namespace NaraGame {

class RHIDevice;
class RHITexture;

// Bundle a Tilemap + the texture its SpriteSheet samples from. The texture
// lives inside the asset because the SpriteSheet inside `map.SetTileset(...)`
// only stores a raw pointer -- if the texture goes out of scope the tilemap's
// regions become dangling. Keep the asset alive for as long as the map is
// drawn.
struct TilemapAsset {
    std::unique_ptr<RHITexture> texture;
    Tilemap                     map;
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
// the .ntmap file's directory. Returns false on I/O failure.
bool SaveNtmap(const std::string& path, const Tilemap& map,
               const std::string& tilesetPath, int tileWidth, int tileHeight,
               int margin = 0, int spacing = 0);

} // namespace NaraGame
