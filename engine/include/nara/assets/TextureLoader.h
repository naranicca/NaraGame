#pragma once

#include <memory>
#include <string>

namespace nara {

class RHIDevice;
class RHITexture;
struct Image;

// Create a GPU texture from an already-decoded image.
std::unique_ptr<RHITexture> CreateTexture(RHIDevice& device, const Image& image);

// Load an image file and upload it as a GPU texture. Returns nullptr on failure.
std::unique_ptr<RHITexture> LoadTexture(RHIDevice& device, const std::string& path);

} // namespace nara
