#include "nara/renderer/SpriteSheet.h"

#include "nara/renderer/rhi/RHIResources.h"

namespace nara {

SpriteSheet::SpriteSheet(RHITexture* texture, int cellWidth, int cellHeight, int margin, int spacing)
    : m_texture(texture)
    , m_cellWidth(cellWidth)
    , m_cellHeight(cellHeight)
    , m_margin(margin)
    , m_spacing(spacing) {
    if (texture == nullptr || cellWidth <= 0 || cellHeight <= 0)
        return;

    const int tw = static_cast<int>(texture->Width());
    const int th = static_cast<int>(texture->Height());

    // How many cells fit, accounting for margin on both sides and spacing
    // between cells: count * cell + (count - 1) * spacing + 2 * margin <= size.
    if (tw >= 2 * margin + cellWidth)
        m_columns = (tw - 2 * margin + spacing) / (cellWidth + spacing);
    if (th >= 2 * margin + cellHeight)
        m_rows = (th - 2 * margin + spacing) / (cellHeight + spacing);
}

TextureRegion SpriteSheet::At(int column, int row) const {
    if (m_texture == nullptr || column < 0 || row < 0 || column >= m_columns || row >= m_rows)
        return {};

    const float x = static_cast<float>(m_margin + column * (m_cellWidth + m_spacing));
    const float y = static_cast<float>(m_margin + row * (m_cellHeight + m_spacing));
    return TextureRegion::FromPixels(m_texture, x, y, static_cast<float>(m_cellWidth),
                                     static_cast<float>(m_cellHeight));
}

TextureRegion SpriteSheet::At(int index) const {
    if (m_columns <= 0)
        return {};
    return At(index % m_columns, index / m_columns);
}

} // namespace nara
