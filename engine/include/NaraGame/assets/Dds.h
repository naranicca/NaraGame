#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace NaraGame {

// Loaded DDS cubemap with RGBA float (half -> single) face pixels. The
// loader keeps the pixel layout the engine's RHI expects -- six faces
// in (+X, -X, +Y, -Y, +Z, -Z) order, each `faceSize * faceSize` pixels.
// Only the base mip is decoded; mip chains are skipped for now since
// the IBL bake re-generates the roughness levels itself.
struct DdsCubeImage {
    uint32_t faceSize = 0;
    // Interleaved RGB floats per pixel. Size = 6 * faceSize * faceSize * 3.
    std::vector<float> pixels;
    bool Valid() const {
        return faceSize > 0 &&
               pixels.size() == size_t(6) * faceSize * faceSize * 3;
    }
};

// Load a DDS cubemap from disk. Currently supports DX10-extension headers
// with `DXGI_FORMAT_R16G16B16A16_FLOAT` (the standard HDR cube format
// used by Khronos PBR sample assets). Returns an empty DdsCubeImage on
// any parse failure (the error goes to the log).
DdsCubeImage LoadDdsCube(const std::string& path);

} // namespace NaraGame
