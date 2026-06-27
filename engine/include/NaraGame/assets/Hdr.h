#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace NaraGame {

// Decoded Radiance .hdr (RGBE) image. Equirectangular HDR environment
// maps are the standard shape -- width is azimuth, height is elevation.
// Pixels are interleaved RGB floats (no alpha); use `width * y + x` to
// index by pixel coord and `* 3 + channel` to pick R/G/B.
struct HdrImage {
    uint32_t width  = 0;
    uint32_t height = 0;
    std::vector<float> pixels;  // width * height * 3 floats

    bool Valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * height * 3;
    }
};

// Load a Radiance .hdr (.pic) file from disk. The decoder accepts both
// the RLE-encoded "new format" scanline layout and uncompressed scanlines.
// Returns an empty HdrImage on parse failure (the error goes to the log).
HdrImage LoadHdrFile(const std::string& path);

} // namespace NaraGame
