#pragma once

#include "NaraGame/math/Vec2.h"
#include "NaraGame/renderer/TextureRegion.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace NaraGame {

class RHITexture;

// One glyph's atlas location plus its em-relative quad layout. Distance fields
// need their own struct (rather than reusing FontGlyph) because MSDF uses two
// independent rects per glyph: the atlas rect carries the precomputed distance
// field with a small padding around the visible glyph (the "pxRange" spread),
// while the plane rect is the exact em-relative quad to draw on screen.
//
// All atlas* fields are pixel coords in the atlas texture (msdf-atlas-gen
// reports them with sub-texel precision, so they're floats). All plane* fields
// are em units relative to the baseline pen position; +Y points down (atlas
// was generated with -yorigin top to match the engine's screen-space).
struct MSDFGlyph {
    float atlasLeft   = 0.0f, atlasTop    = 0.0f;
    float atlasRight  = 0.0f, atlasBottom = 0.0f;
    float planeLeft   = 0.0f, planeTop    = 0.0f;
    float planeRight  = 0.0f, planeBottom = 0.0f;
    float advance     = 0.0f;
};

// MSDF font backed by one atlas texture + per-glyph metadata. Stays sharp at
// any draw size because the fragment shader (Renderer2D's MSDF pipeline)
// reconstructs the glyph edge from a distance field rather than sampling
// a pre-rasterised bitmap. See engine/shaders/msdf.frag for the math.
//
// `DrawText` takes a `pixelSize` argument that is the em-box height in screen
// pixels — i.e. callers pass the final text size directly, not a multiplier.
// (Different from BitmapFont/Font where `scale` is a multiplier on a baked
// glyph size; MSDF has no native pixel size, so a direct height is cleaner.)
class MSDFFont {
public:
    MSDFFont() = default;

    bool Valid() const { return m_texture != nullptr; }
    RHITexture* Texture() const { return m_texture.get(); }

    const MSDFGlyph* Find(uint32_t codepoint) const;
    TextureRegion Region(const MSDFGlyph& glyph) const;
    Vec2 Measure(const std::string& text, float pixelSize) const;

    float LineHeight() const { return m_lineHeight; }
    float Ascender()   const { return m_ascender; }   // em units; negative
    float Descender()  const { return m_descender; }  // em units; positive
    float PxRange()    const { return m_pxRange; }
    uint32_t AtlasWidth()  const { return m_atlasWidth; }
    uint32_t AtlasHeight() const { return m_atlasHeight; }

    // --- Used by loaders ---
    void SetTexture(std::unique_ptr<RHITexture> texture, uint32_t w, uint32_t h);
    void AddGlyph(uint32_t codepoint, const MSDFGlyph& g) { m_glyphs[codepoint] = g; }
    void SetMetrics(float lineHeight, float ascender, float descender) {
        m_lineHeight = lineHeight; m_ascender = ascender; m_descender = descender;
    }
    void SetPxRange(float r) { m_pxRange = r; }

private:
    std::unique_ptr<RHITexture> m_texture;
    std::unordered_map<uint32_t, MSDFGlyph> m_glyphs;
    uint32_t m_atlasWidth  = 0;
    uint32_t m_atlasHeight = 0;
    float m_lineHeight = 1.2f;
    float m_ascender   = 0.0f;
    float m_descender  = 0.0f;
    float m_pxRange    = 4.0f;
};

} // namespace NaraGame
