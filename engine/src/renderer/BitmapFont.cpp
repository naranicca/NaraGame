#include "NaraGame/renderer/BitmapFont.h"

#include "NaraGame/renderer/rhi/RHIResources.h"

namespace NaraGame {

BitmapFont::BitmapFont(RHITexture* texture, char firstChar, int columns, int glyphWidth,
                       int glyphHeight, int margin, int spacing)
    : m_texture(texture)
    , m_firstChar(firstChar)
    , m_columns(columns)
    , m_glyphWidth(glyphWidth)
    , m_glyphHeight(glyphHeight)
    , m_margin(margin)
    , m_spacing(spacing) {}

TextureRegion BitmapFont::Glyph(char c) const {
    if (m_texture == nullptr || m_columns <= 0 || c < m_firstChar)
        return {};

    const int index = c - m_firstChar;
    const int col = index % m_columns;
    const int row = index / m_columns;
    const float x = static_cast<float>(m_margin + col * (m_glyphWidth + m_spacing));
    const float y = static_cast<float>(m_margin + row * (m_glyphHeight + m_spacing));
    return TextureRegion::FromPixels(m_texture, x, y, static_cast<float>(m_glyphWidth),
                                     static_cast<float>(m_glyphHeight));
}

Vec2 BitmapFont::Measure(const std::string& text, float scale) const {
    float maxWidth = 0.0f;
    float lineWidth = 0.0f;
    int lines = 1;
    for (char c : text) {
        if (c == '\n') {
            maxWidth = lineWidth > maxWidth ? lineWidth : maxWidth;
            lineWidth = 0.0f;
            ++lines;
            continue;
        }
        lineWidth += static_cast<float>(Advance());
    }
    maxWidth = lineWidth > maxWidth ? lineWidth : maxWidth;
    return Vec2{maxWidth * scale, static_cast<float>(lines * LineHeight()) * scale};
}

} // namespace NaraGame
