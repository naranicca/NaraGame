#pragma once

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>

#include <cstdint>
#include <string>
#include <vector>

namespace SceneEditor {

// One sub-rectangle inside a sprite sheet texture. Coordinates are in
// pixel space (origin at the texture's top-left), pivot is normalised
// (0..1) just like Unity's Sprite Editor, border holds the 9-slice
// margins for Sliced draw mode (left / right / top / bottom in pixels;
// all zero for plain sprites).
struct SubSprite {
    std::string    name;
    int            x = 0, y = 0, w = 0, h = 0;
    NaraGame::Vec2 pivot{0.5f, 0.5f};
    int            borderLeft = 0, borderRight  = 0;
    int            borderTop  = 0, borderBottom = 0;
};

// A texture cut into one or more sub-sprites. The Sprite Editor edits
// this list; SpriteRenderer references a particular sub-sprite by its
// index inside the parent texture asset.
struct SpriteSheet {
    std::vector<SubSprite> sprites;

    void FromJson(const NaraGame::MiniJson::Value& obj);
    NaraGame::MiniJson::Value ToJson() const;
};

// Slice a texture of the given pixel size into a regular grid by cell
// dimensions (cellW x cellH starting at the top-left, scanning rows
// left-to-right top-to-bottom). Returns the new sub-sprite list. The
// `namePrefix` is the file stem; generated names are "<prefix>_<n>"
// with zero-based n.
std::vector<SubSprite> SliceByCellSize(int texWidth, int texHeight,
                                       int cellW, int cellH,
                                       const std::string& namePrefix);

// Slice into `cols * rows` cells of equal size. Cells are integer-floor
// of (texSize / count), so any remainder pixels on the right / bottom
// are left out (matches Unity's "by cell count" behaviour).
std::vector<SubSprite> SliceByCellCount(int texWidth, int texHeight,
                                        int cols, int rows,
                                        const std::string& namePrefix);

} // namespace SceneEditor
