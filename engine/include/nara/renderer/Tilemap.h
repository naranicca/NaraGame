#pragma once

#include "nara/math/Vec2.h"
#include "nara/math/Vec4.h"
#include "nara/renderer/SpriteSheet.h"
#include "nara/renderer/rhi/RHITypes.h"

#include <vector>

namespace nara {

class Renderer2D;

// A grid of tile indices drawn from a tileset SpriteSheet. Tile id -1 is empty.
// Draw() culls to a visible world rectangle so only on-screen tiles are emitted.
class Tilemap {
public:
    Tilemap() = default;
    Tilemap(int width, int height, float tileWidth, float tileHeight);

    void SetTileset(const SpriteSheet& tileset) { m_tileset = tileset; }

    int Width() const { return m_width; }
    int Height() const { return m_height; }
    float TileWidth() const { return m_tileWidth; }
    float TileHeight() const { return m_tileHeight; }
    Vec2 WorldSize() const { return {m_width * m_tileWidth, m_height * m_tileHeight}; }

    void Fill(int tileId);
    void SetTile(int x, int y, int tileId);
    int GetTile(int x, int y) const;

    // Draw the tiles overlapping `visible` = (minX, minY, maxX, maxY) in world space.
    void Draw(Renderer2D& renderer, const Vec4& visible, const Color& tint = Color::White()) const;

private:
    int m_width = 0;
    int m_height = 0;
    float m_tileWidth = 0.0f;
    float m_tileHeight = 0.0f;
    SpriteSheet m_tileset;
    std::vector<int> m_tiles; // row-major; -1 = empty
};

} // namespace nara
