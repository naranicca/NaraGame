#include "NaraGame/assets/TextureLoader.h"

#include "NaraGame/assets/Image.h"
#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"

namespace NaraGame {

std::unique_ptr<RHITexture> CreateTexture(RHIDevice& device, const Image& image,
                                          TextureFilter filter) {
    if (!image.Valid()) {
        NARAGAME_ERROR("CreateTexture: invalid image");
        return nullptr;
    }
    TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.format = TextureFormat::RGBA8;
    desc.filter = filter;
    desc.pixels = image.pixels.data();
    return device.CreateTexture(desc);
}

std::unique_ptr<RHITexture> LoadTexture(RHIDevice& device, const std::string& path,
                                        TextureFilter filter) {
    const Image image = LoadImageFile(path);
    if (!image.Valid()) {
        NARAGAME_ERROR("LoadTexture: failed to load '%s'", path.c_str());
        return nullptr;
    }
    NARAGAME_INFO("Loaded texture '%s' (%ux%u)", path.c_str(), image.width, image.height);
    return CreateTexture(device, image, filter);
}

bool ReloadTexture(RHITexture& texture, const std::string& path) {
    const Image image = LoadImageFile(path);
    if (!image.Valid()) {
        NARAGAME_WARN("ReloadTexture: failed to load '%s'", path.c_str());
        return false;
    }
    if (image.width != texture.Width() || image.height != texture.Height()) {
        NARAGAME_WARN("ReloadTexture: '%s' size %ux%u != live texture %ux%u",
                      path.c_str(), image.width, image.height,
                      texture.Width(), texture.Height());
        return false;
    }
    if (!texture.UpdatePixels(image.pixels.data())) {
        NARAGAME_WARN("ReloadTexture: backend does not support in-place updates");
        return false;
    }
    NARAGAME_INFO("Reloaded texture '%s' (%ux%u)", path.c_str(), image.width, image.height);
    return true;
}

} // namespace NaraGame
