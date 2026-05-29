#pragma once

#include "NaraGame/renderer/rhi/RHITypes.h"

#include <memory>
#include <string>

namespace NaraGame {

class RHIDevice;
class RHITexture;
struct Image;

// Create a GPU texture from an already-decoded image. `filter` controls how
// the texture is sampled at draw time -- Linear for soft sprites, Point for
// pixel-art (bitmap fonts, tile sheets) that must not be blurred on upscale.
std::unique_ptr<RHITexture> CreateTexture(RHIDevice& device, const Image& image,
                                          TextureFilter filter = TextureFilter::Linear);

// Load an image file and upload it as a GPU texture. Returns nullptr on failure.
std::unique_ptr<RHITexture> LoadTexture(RHIDevice& device, const std::string& path,
                                        TextureFilter filter = TextureFilter::Linear);

} // namespace NaraGame
