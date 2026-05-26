#include "NaraGame/renderer/TextureRegion.h"

#include "NaraGame/renderer/rhi/RHIResources.h"

namespace NaraGame {

TextureRegion TextureRegion::FromPixels(RHITexture* texture, float x, float y, float w, float h) {
    if (texture == nullptr)
        return {};

    const float tw = static_cast<float>(texture->Width());
    const float th = static_cast<float>(texture->Height());
    if (tw <= 0.0f || th <= 0.0f)
        return {};

    const Vec4 uv{x / tw, y / th, (x + w) / tw, (y + h) / th};
    return TextureRegion(texture, uv, Vec2{w, h});
}

} // namespace NaraGame
