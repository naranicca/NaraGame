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

// Load `path` and push its pixels into an existing texture without replacing
// the GPU object. Used by hot reload so atlas region pointers and SRVs that
// already reference `texture` stay valid. Fails (returning false and leaving
// the texture untouched) when the file can't be loaded or its dimensions
// no longer match the live texture -- a resized art file needs a real
// re-create, not a pixel swap.
bool ReloadTexture(RHITexture& texture, const std::string& path);

} // namespace NaraGame
