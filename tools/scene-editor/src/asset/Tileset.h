#pragma once

#include "AssetGuid.h"

#include <NaraGame/assets/MiniJson.h>

#include <map>
#include <string>

namespace SceneEditor {

// Per-tile metadata stored inside a Tileset. Sparse -- only the tiles
// the user has flagged end up in the map; defaults (collides = false)
// fall through automatically.
// M7c21: per-tile collider shape. Box behaves like a regular
// AABB / OBB (the M7c13 SAT path); OneWay drops collisions that
// would push the body sideways or downward, so a falling body
// lands on the platform but a body jumping up from below passes
// right through. Useful for classic 2D-platformer "jump-through"
// platforms.
enum class TileColliderShape {
    Box,
    OneWay,
    // M7c22: 45-degree slope tiles. The hypotenuse runs corner-
    // to-corner of the cell + the bottom triangle is filled,
    // so a body rolling into the slope from the side climbs
    // along the diagonal.
    //   SlopeRight (/): hypotenuse bottom-left -> top-right;
    //                   filled bottom-right triangle.
    //   SlopeLeft  (\): hypotenuse bottom-right -> top-left;
    //                   filled bottom-left triangle.
    SlopeRight,
    SlopeLeft,
};

struct TilesetTile {
    bool collides = false;
    // M7c19: trigger-mode collision. When set alongside
    // `collides`, the per-tile synthetic collider fires
    // OnTriggerEnter/Stay/Exit instead of applying push-out --
    // useful for damage zones, pickups baked into the tilemap,
    // door / portal cells, etc.
    bool isTrigger = false;
    // M7c21: collider shape selector (Box / OneWay). OneWay is
    // ignored when isTrigger is true -- the M7c19 trigger path
    // doesn't push, so the directional semantics don't apply.
    TileColliderShape shape = TileColliderShape::Box;
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
