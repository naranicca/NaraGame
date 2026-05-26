#pragma once

#include "NaraGame/renderer/TextureRegion.h"

namespace NaraGame {

class RHITexture;

// Slices a texture into a uniform grid of cells — the typical layout for
// tilesets and animation frames. `margin` is the border around the whole sheet;
// `spacing` is the gap between adjacent cells.
class SpriteSheet {
public:
    SpriteSheet() = default;
    SpriteSheet(RHITexture* texture, int cellWidth, int cellHeight, int margin = 0, int spacing = 0);

    int Columns() const { return m_columns; }
    int Rows() const { return m_rows; }
    int Count() const { return m_columns * m_rows; }
    RHITexture* Texture() const { return m_texture; }

    TextureRegion At(int column, int row) const;
    TextureRegion At(int index) const; // row-major; 0 = top-left

private:
    RHITexture* m_texture = nullptr;
    int m_cellWidth = 0;
    int m_cellHeight = 0;
    int m_margin = 0;
    int m_spacing = 0;
    int m_columns = 0;
    int m_rows = 0;
};

} // namespace NaraGame
