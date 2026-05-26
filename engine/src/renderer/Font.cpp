#include "nara/renderer/Font.h"

#include "nara/renderer/rhi/RHIResources.h"

namespace nara {

Font::Font() = default;
Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

const FontGlyph* Font::Find(uint32_t codepoint) const {
    const auto it = m_glyphs.find(codepoint);
    return it != m_glyphs.end() ? &it->second : nullptr;
}

TextureRegion Font::Region(const FontGlyph& glyph) const {
    return TextureRegion::FromPixels(m_texture.get(), glyph.x, glyph.y, glyph.width, glyph.height);
}

Vec2 Font::Measure(const std::string& text, float scale) const {
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
        if (const FontGlyph* g = Find(static_cast<uint32_t>(static_cast<unsigned char>(c))))
            lineWidth += g->xadvance;
    }
    maxWidth = lineWidth > maxWidth ? lineWidth : maxWidth;
    return Vec2{maxWidth * scale, static_cast<float>(lines) * m_lineHeight * scale};
}

void Font::SetTexture(std::unique_ptr<RHITexture> texture) {
    m_texture = std::move(texture);
}

} // namespace nara
