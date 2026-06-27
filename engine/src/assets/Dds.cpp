#include "NaraGame/assets/Dds.h"

#include "NaraGame/core/Log.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace NaraGame {

namespace {

// Field flags from the DDS spec we actually care about. The full set
// lives in DirectXTK's DDS_FORMAT.h; we only test the cube + DX10 bits.
constexpr uint32_t kDdsFourCc       = 0x00000004;  // ddspf.dwFlags
constexpr uint32_t kDdpfDx10        = 0x30315844;  // 'DX10' little-endian
constexpr uint32_t kCaps2Cubemap    = 0x00000200;  // header.dwCaps2
// All six face flags must be set for a full cube. (Partial cubes exist
// but we'd warn -- the IBL pipeline needs all six.)
constexpr uint32_t kCubeAllFaces    = 0x0000FA00;
constexpr uint32_t kMiscFlagCube    = 0x00000004;  // DX10 ext.miscFlag

constexpr uint32_t kDxgiR16G16B16A16Float = 10;

// IEEE 754 binary16 -> binary32 decode. Subnormals collapse to zero
// (matching the encoder side); Inf and NaN passes through with the same
// sign bit.
float HalfToFloat(uint16_t h) {
    const uint32_t sign     = (uint32_t(h) & 0x8000u) << 16;
    const uint32_t expBits  = (uint32_t(h) >> 10) & 0x1Fu;
    const uint32_t mantissa =  uint32_t(h)        & 0x03FFu;
    if (expBits == 0)  return 0.0f;
    if (expBits == 31) {
        const uint32_t bits = sign | 0x7F800000u | (mantissa << 13);
        float out;
        std::memcpy(&out, &bits, 4);
        return out;
    }
    const uint32_t bits = sign | ((expBits + 127 - 15) << 23) | (mantissa << 13);
    float out;
    std::memcpy(&out, &bits, 4);
    return out;
}

uint32_t ReadU32(const uint8_t* p) {
    return  uint32_t(p[0])
         | (uint32_t(p[1]) <<  8)
         | (uint32_t(p[2]) << 16)
         | (uint32_t(p[3]) << 24);
}

uint16_t ReadU16(const uint8_t* p) {
    return  uint16_t(p[0])
         | (uint16_t(p[1]) << 8);
}

} // namespace

DdsCubeImage LoadDdsCube(const std::string& path) {
    DdsCubeImage out;

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        NARAGAME_ERROR("LoadDdsCube: cannot open '%s'", path.c_str());
        return out;
    }
    const std::streamsize fileSize = in.tellg();
    in.seekg(0, std::ios::beg);
    // Two-step (declare + resize) form sidesteps the most-vexing-parse
    // that `std::vector<uint8_t> bytes(size_t(fileSize));` would hit on
    // MSVC: the compiler treats it as a function declaration.
    std::vector<uint8_t> bytes;
    bytes.resize(size_t(fileSize));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
        NARAGAME_ERROR("LoadDdsCube: short read '%s'", path.c_str());
        return out;
    }
    if (bytes.size() < 128) {
        NARAGAME_ERROR("LoadDdsCube: '%s' truncated (< 128 bytes)", path.c_str());
        return out;
    }

    // Magic "DDS " then a 124-byte DDS_HEADER struct.
    if (bytes[0] != 'D' || bytes[1] != 'D' ||
        bytes[2] != 'S' || bytes[3] != ' ') {
        NARAGAME_ERROR("LoadDdsCube: '%s' missing DDS magic", path.c_str());
        return out;
    }
    const uint8_t* hdr = bytes.data() + 4;
    const uint32_t hdrSize = ReadU32(hdr);
    if (hdrSize != 124) {
        NARAGAME_ERROR("LoadDdsCube: '%s' header size %u (expected 124)",
                       path.c_str(), hdrSize);
        return out;
    }
    const uint32_t height   = ReadU32(hdr + 8);
    const uint32_t width    = ReadU32(hdr + 12);
    // const uint32_t mipCount = ReadU32(hdr + 24);  // we only decode mip 0
    const uint8_t* ddspf    = hdr + 72;
    const uint32_t pfFlags  = ReadU32(ddspf + 4);
    const uint32_t pfFourCc = ReadU32(ddspf + 8);
    // Layout past ddspf: dwCaps @ hdr+104, dwCaps2 @ hdr+108.
    const uint32_t caps2    = ReadU32(hdr + 108);

    if (width != height) {
        NARAGAME_ERROR("LoadDdsCube: '%s' %ux%u faces must be square",
                       path.c_str(), width, height);
        return out;
    }

    const bool hasDx10 = (pfFlags & kDdsFourCc) != 0 && pfFourCc == kDdpfDx10;
    if (!hasDx10) {
        NARAGAME_ERROR("LoadDdsCube: '%s' lacks DX10 extension header "
                       "(only DX10 + R16G16B16A16_FLOAT is supported)",
                       path.c_str());
        return out;
    }
    if (bytes.size() < 128 + 20) {
        NARAGAME_ERROR("LoadDdsCube: '%s' truncated DX10 extension",
                       path.c_str());
        return out;
    }
    const uint8_t* dx10     = bytes.data() + 128;
    const uint32_t dxgiFmt  = ReadU32(dx10 + 0);
    // const uint32_t resDim   = ReadU32(dx10 + 4);
    const uint32_t miscFlag = ReadU32(dx10 + 8);
    const uint32_t arraySz  = ReadU32(dx10 + 12);

    if (dxgiFmt != kDxgiR16G16B16A16Float) {
        NARAGAME_ERROR("LoadDdsCube: '%s' DXGI format %u not supported "
                       "(need R16G16B16A16_FLOAT = 10)",
                       path.c_str(), dxgiFmt);
        return out;
    }
    const bool isCube = (miscFlag & kMiscFlagCube) != 0
                     || (caps2 & kCaps2Cubemap) != 0;
    if (!isCube) {
        NARAGAME_ERROR("LoadDdsCube: '%s' is not a cubemap", path.c_str());
        return out;
    }
    if ((caps2 & kCubeAllFaces) != kCubeAllFaces) {
        NARAGAME_WARN("LoadDdsCube: '%s' caps2 0x%x missing some face flags",
                      path.c_str(), caps2);
    }
    if (arraySz != 0 && arraySz != 1) {
        NARAGAME_WARN("LoadDdsCube: '%s' arraySize %u; reading first cube only",
                      path.c_str(), arraySz);
    }

    // RGBA16F = 8 bytes/pixel. Base-mip face data starts right after the
    // DX10 extension. We read exactly 6 face buffers; mip chain (if any)
    // sits past that and gets ignored.
    constexpr size_t kBytesPerPixel = 8;
    const size_t faceBytes = size_t(width) * size_t(height) * kBytesPerPixel;
    const size_t cubeBytes = faceBytes * 6;
    const size_t pixelsOff = 128 + 20;
    if (pixelsOff + cubeBytes > bytes.size()) {
        NARAGAME_ERROR("LoadDdsCube: '%s' truncated pixel data "
                       "(need %zu, have %zu)",
                       path.c_str(), cubeBytes, bytes.size() - pixelsOff);
        return out;
    }

    out.faceSize = width;
    out.pixels.assign(size_t(6) * width * height * 3, 0.0f);
    for (int face = 0; face < 6; ++face) {
        const uint8_t* src = bytes.data() + pixelsOff + size_t(face) * faceBytes;
        for (size_t i = 0; i < size_t(width) * height; ++i) {
            const uint16_t r = ReadU16(src + i * kBytesPerPixel + 0);
            const uint16_t g = ReadU16(src + i * kBytesPerPixel + 2);
            const uint16_t b = ReadU16(src + i * kBytesPerPixel + 4);
            // alpha at +6 ignored.
            const size_t dst = (size_t(face) * size_t(width) * height + i) * 3;
            out.pixels[dst + 0] = HalfToFloat(r);
            out.pixels[dst + 1] = HalfToFloat(g);
            out.pixels[dst + 2] = HalfToFloat(b);
        }
    }

    NARAGAME_INFO("LoadDdsCube: '%s' decoded %ux%u cube (RGBA16F)",
                  path.c_str(), out.faceSize, out.faceSize);
    return out;
}

} // namespace NaraGame
