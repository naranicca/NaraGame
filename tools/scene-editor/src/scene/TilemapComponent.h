#pragma once

#include "../asset/AssetGuid.h"
#include "Component.h"

#include <NaraGame/assets/MiniJson.h>

#include <map>
#include <utility>

namespace SceneEditor {

// 2D tilemap built on top of a tile-sheet texture. The texture is
// sliced into a regular grid by (cellPixelW x cellPixelH); each cell
// in the map references a tile via its row-major index inside that
// grid. Cells are stored sparsely so a half-filled map doesn't have
// to allocate the full rectangle.
//
// M4c1 ships the data model + viewport renderer. Paint tools land in
// M4c2; a proper Tileset asset (with collision flags / properties)
// and tile palette panel land later in M4.
class TilemapComponent final : public Component {
public:
    AssetGuid tileset;                            // source texture GUID
    int       cellPixelW = 16;
    int       cellPixelH = 16;

    // (gridX, gridY) -> tile index. gridY uses world Y-up: gy = 0 is
    // the bottom row, gy > 0 climbs upward. Tile indices are
    // row-major inside the source texture (0 = top-left source cell).
    std::map<std::pair<int, int>, int> cells;

    static const char* StaticTypeName() { return "Tilemap"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
