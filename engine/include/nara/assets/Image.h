#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nara {

// Decoded image, always expanded to 8-bit RGBA, stored top-to-bottom.
struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // width * height * 4 (RGBA)

    bool Valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * height * 4;
    }
};

// Decode a PNG from memory. Returns an invalid Image on failure.
// Supports 8-bit grayscale / grayscale+alpha / RGB / RGBA / palette,
// non-interlaced (the common cases for game art).
Image DecodePNG(const uint8_t* data, size_t size);

// Load an image file from disk. Currently dispatches to the PNG decoder.
Image LoadImageFile(const std::string& path);

} // namespace nara
