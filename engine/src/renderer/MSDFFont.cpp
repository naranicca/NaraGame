#include "NaraGame/renderer/MSDFFont.h"

#include "NaraGame/renderer/rhi/RHIResources.h"

namespace NaraGame {

void MSDFFont::SetTexture(std::unique_ptr<RHITexture> texture, uint32_t w, uint32_t h) {
    m_texture = std::move(texture);
    m_atlasWidth = w;
    m_atlasHeight = h;
}

const MSDFGlyph* MSDFFont::Find(uint32_t codepoint) const {
    const auto it = m_glyphs.find(codepoint);
    return (it == m_glyphs.end()) ? nullptr : &it->second;
}

TextureRegion MSDFFont::Region(const MSDFGlyph& g) const {
    // msdf-atlas-gen reports atlas bounds with .5 sub-texel offsets so the
    // distance field is sampled at pixel centres. FromPixels just divides by
    // atlas size, so those fractions go straight through into UVs — exactly
    // what the linear-filtered MSDF sampler wants.
    return TextureRegion::FromPixels(
        m_texture.get(),
        g.atlasLeft, g.atlasTop,
        g.atlasRight - g.atlasLeft,
        g.atlasBottom - g.atlasTop);
}

Vec2 MSDFFont::Measure(const std::string& text, float pixelSize) const {
    float lineWidth = 0.0f;
    float maxWidth  = 0.0f;
    int   lines     = 1;
    for (char c : text) {
        if (c == '\n') {
            maxWidth = (lineWidth > maxWidth) ? lineWidth : maxWidth;
            lineWidth = 0.0f;
            ++lines;
            continue;
        }
        if (const MSDFGlyph* g = Find(static_cast<uint32_t>(static_cast<unsigned char>(c)))) {
            lineWidth += g->advance * pixelSize;
        }
    }
    maxWidth = (lineWidth > maxWidth) ? lineWidth : maxWidth;
    return Vec2{maxWidth, m_lineHeight * pixelSize * static_cast<float>(lines)};
}

} // namespace NaraGame
