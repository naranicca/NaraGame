#pragma once

#include "AssetGuid.h"

#include <NaraGame/assets/MiniJson.h>

#include <array>
#include <set>
#include <string>

namespace SceneEditor {

// 4-bit cardinal-neighbour autotile rule. The 16-entry mask table maps
// (north, east, south, west) presence bits -> visual tile index inside
// the source texture (row-major, same indexing the TilemapComponent
// uses for its raw cells).
//
// Bit layout:
//   bit 0 = N neighbour present
//   bit 1 = E neighbour present
//   bit 2 = S neighbour present
//   bit 3 = W neighbour present
//
// So mask[0] is the lonely / standalone tile and mask[15] is the fully
// surrounded centre.
class AutotileAsset {
public:
    AssetGuid              texture;          // source texture asset GUID
    int                    cellPixelW = 16;
    int                    cellPixelH = 16;
    std::array<int, 16>    mask{};           // mask -> tile index

    // Set of every tile index that appears in the mask table. The paint
    // flow uses this to decide whether a neighbour belongs to the
    // autotile group when recomputing masks; rebuilt on every load /
    // SetMask.
    std::set<int> tileSet;

    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    void FromJson(const NaraGame::MiniJson::Value& root);
    NaraGame::MiniJson::Value ToJson() const;

    // Rebuild the tileSet from the current mask table. Called by
    // FromJson and any future editor mutation path.
    void RebuildTileSet();
};

} // namespace SceneEditor
