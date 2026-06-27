#pragma once

#include "AssetGuid.h"

#include <NaraGame/assets/MiniJson.h>

#include <map>
#include <string>

namespace SceneEditor {

// Per-tile metadata stored inside a Tileset. Sparse -- only the tiles
// the user has flagged end up in the map; defaults (collides = false)
// fall through automatically.
struct TilesetTile {
    bool collides = false;
};

// .ntileset asset: a named slice of a tileset texture + per-tile
// metadata. Lives as a JSON file alongside the texture in
// Assets/Tilesets, with the usual .meta sidecar that AssetDatabase
// manages for every asset.
//
// TilemapComponent can now point at one of these instead of a raw
// texture; the renderer resolves the tileset to its texture +
// cellPixelW/H so the per-component cellPixel fields become a
// fallback for the "tilemap points straight at a texture" path.
class TilesetAsset {
public:
    AssetGuid texture;          // source texture asset GUID
    int       cellPixelW = 16;
    int       cellPixelH = 16;
    std::map<int, TilesetTile> tiles;   // sparse index -> tile data

    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    void FromJson(const NaraGame::MiniJson::Value& root);
    NaraGame::MiniJson::Value ToJson() const;
};

} // namespace SceneEditor
