#include "NaraGame/renderer/Tilemap.h"

#include "NaraGame/math/MathUtil.h"
#include "NaraGame/renderer/Renderer2D.h"

#include <cmath>

namespace NaraGame {

Tilemap::Tilemap(int width, int height, float tileWidth, float tileHeight)
    : m_width(width)
    , m_height(height)
    , m_tileWidth(tileWidth)
    , m_tileHeight(tileHeight)
    , m_tiles(static_cast<size_t>(width) * height, -1) {}

void Tilemap::Fill(int tileId) {
    for (int& tile : m_tiles)
        tile = tileId;
}

void Tilemap::SetTile(int x, int y, int tileId) {
    if (x >= 0 && y >= 0 && x < m_width && y < m_height)
        m_tiles[static_cast<size_t>(y) * m_width + x] = tileId;
}

int Tilemap::GetTile(int x, int y) const {
    if (x >= 0 && y >= 0 && x < m_width && y < m_height)
        return m_tiles[static_cast<size_t>(y) * m_width + x];
    return -1;
}

void Tilemap::Draw(Renderer2D& renderer, const Vec4& visible, const Color& tint) const {
    if (m_tileWidth <= 0.0f || m_tileHeight <= 0.0f)
        return;

    // Restrict iteration to the tile range overlapping the visible rectangle.
    const int x0 = Max(0, static_cast<int>(std::floor(visible.x / m_tileWidth)));
    const int y0 = Max(0, static_cast<int>(std::floor(visible.y / m_tileHeight)));
    const int x1 = Min(m_width - 1, static_cast<int>(std::floor(visible.z / m_tileWidth)));
    const int y1 = Min(m_height - 1, static_cast<int>(std::floor(visible.w / m_tileHeight)));

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int id = m_tiles[static_cast<size_t>(y) * m_width + x];
            if (id < 0)
                continue;
            const TextureRegion region = m_tileset.At(id);
            if (!region.Valid())
                continue;
            renderer.DrawQuad({x * m_tileWidth, y * m_tileHeight}, {m_tileWidth, m_tileHeight},
                              region, tint);
        }
    }
}

} // namespace NaraGame
