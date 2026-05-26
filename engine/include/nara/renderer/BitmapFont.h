#pragma once

#include "nara/math/Vec2.h"
#include "nara/renderer/TextureRegion.h"

#include <string>

namespace nara {

class RHITexture;

// Monospaced bitmap font. Glyphs live in a uniform grid starting at `firstChar`,
// laid out left-to-right then top-to-bottom (the same layout SpriteSheet uses).
// A proportional / BMFont-metrics variant can layer on top later.
class BitmapFont {
public:
    BitmapFont() = default;
    BitmapFont(RHITexture* texture, char firstChar, int columns, int glyphWidth, int glyphHeight,
               int margin = 0, int spacing = 0);

    bool Valid() const { return m_texture != nullptr; }
    RHITexture* Texture() const { return m_texture; }

    int GlyphWidth() const { return m_glyphWidth; }
    int GlyphHeight() const { return m_glyphHeight; }

    // Horizontal advance per character and vertical advance per line.
    int Advance() const { return m_glyphWidth + m_tracking; }
    int LineHeight() const { return m_glyphHeight + m_lineSpacing; }

    void SetTracking(int pixels) { m_tracking = pixels; }     // extra px between glyphs
    void SetLineSpacing(int pixels) { m_lineSpacing = pixels; }

    // Atlas region for a character (invalid region if out of range).
    TextureRegion Glyph(char c) const;

    // Pixel extent of `text` (handles '\n'), at the given scale.
    Vec2 Measure(const std::string& text, float scale = 1.0f) const;

private:
    RHITexture* m_texture = nullptr;
    char m_firstChar = ' ';
    int m_columns = 0;
    int m_glyphWidth = 0;
    int m_glyphHeight = 0;
    int m_margin = 0;
    int m_spacing = 0;
    int m_tracking = 0;
    int m_lineSpacing = 2;
};

} // namespace nara
