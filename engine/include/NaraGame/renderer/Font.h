#pragma once

#include "NaraGame/math/Vec2.h"
#include "NaraGame/renderer/TextureRegion.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace NaraGame {

class RHITexture;

// One glyph's placement in the page texture plus its layout metrics
// (AngelCode BMFont semantics).
struct FontGlyph {
    float x = 0.0f, y = 0.0f;       // pixel rect in the page texture
    float width = 0.0f, height = 0.0f;
    float xoffset = 0.0f, yoffset = 0.0f; // offset from the pen position
    float xadvance = 0.0f;                 // how far the pen moves after this glyph
};

// Proportional (variable-width) font backed by a single page texture. Built by
// the BMFont loader; rendered via Renderer2D::DrawText.
class Font {
public:
    Font();
    ~Font();
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;

    bool Valid() const { return m_texture != nullptr; }
    RHITexture* Texture() const { return m_texture.get(); }
    float LineHeight() const { return m_lineHeight; }

    const FontGlyph* Find(uint32_t codepoint) const;
    TextureRegion Region(const FontGlyph& glyph) const;
    Vec2 Measure(const std::string& text, float scale = 1.0f) const;

    // --- Used by loaders ---
    void SetTexture(std::unique_ptr<RHITexture> texture);
    void SetLineHeight(float lineHeight) { m_lineHeight = lineHeight; }
    void SetBase(float base) { m_base = base; }
    void AddGlyph(uint32_t codepoint, const FontGlyph& glyph) { m_glyphs[codepoint] = glyph; }

private:
    std::unique_ptr<RHITexture> m_texture;
    std::unordered_map<uint32_t, FontGlyph> m_glyphs;
    float m_lineHeight = 0.0f;
    float m_base = 0.0f;
};

} // namespace NaraGame
