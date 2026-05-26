#pragma once

#include "nara/math/Vec2.h"
#include "nara/math/Vec4.h"

namespace nara {

class RHITexture;

// A rectangular sub-region of a texture, expressed as normalized UVs plus the
// region's pixel size. Pass it to Renderer2D to draw a single sprite out of an
// atlas or sprite sheet without computing UVs by hand.
struct TextureRegion {
    RHITexture* texture = nullptr;
    Vec4 uv = {0.0f, 0.0f, 1.0f, 1.0f}; // (u0, v0, u1, v1), v0 = top
    Vec2 size = {0.0f, 0.0f};           // pixel dimensions of the region

    TextureRegion() = default;
    TextureRegion(RHITexture* tex, const Vec4& uvRect, const Vec2& sizePx)
        : texture(tex), uv(uvRect), size(sizePx) {}

    bool Valid() const { return texture != nullptr; }

    // Build a region from a pixel rectangle within `texture`.
    static TextureRegion FromPixels(RHITexture* texture, float x, float y, float w, float h);
};

} // namespace nara
