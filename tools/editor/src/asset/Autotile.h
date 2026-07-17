#pragma once

#include "AssetGuid.h"

#include <NaraGame/assets/MiniJson.h>

#include <array>
#include <set>
#include <string>

namespace SceneEditor {

// Autotile rule. Two modes are supported:
//   Cardinal  -- 4-bit neighbour bitmask (N, E, S, W). 16-entry table.
//                Bit layout: bit 0 = N, 1 = E, 2 = S, 3 = W.
//   Blob      -- 8-bit neighbour bitmask (N, NE, E, SE, S, SW, W, NW).
//                256-entry table; non-canonical entries (a diagonal
//                whose adjacent cardinals are missing) shadow their
//                canonical equivalents at load time. There are ~47
//                canonical masks, the standard "blob tileset".
//                Bit layout: 0 = N, 1 = NE, 2 = E, 3 = SE, 4 = S,
//                5 = SW, 6 = W, 7 = NW.
class AutotileAsset {
public:
    enum class Mode { Cardinal, Blob };
    Mode                   mode = Mode::Cardinal;

    AssetGuid              texture;          // source texture asset GUID
    int                    cellPixelW = 16;
    int                    cellPixelH = 16;
    std::array<int, 16>    mask{};           // Cardinal mode
    std::array<int, 256>   blobMask{};       // Blob mode

    // Set of every tile index reachable from the active mode's table.
    // The paint flow uses it to decide whether a neighbour belongs to
    // the autotile group when recomputing masks; rebuilt on every
    // load / mutation.
    std::set<int> tileSet;

    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    void FromJson(const NaraGame::MiniJson::Value& root);
    NaraGame::MiniJson::Value ToJson() const;

    // Rebuild the tileSet from whichever table the current mode uses.
    void RebuildTileSet();

    // Drop diagonal bits whose adjacent cardinals are missing. NE
    // requires N + E; SE requires E + S; SW requires S + W; NW
    // requires W + N.
    static int Canonicalize8bit(int m);

    // True when the 8-bit mask `m` is equal to its canonical form.
    static bool IsCanonical8bit(int m) { return m == Canonicalize8bit(m); }
};

} // namespace SceneEditor
