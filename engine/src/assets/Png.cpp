#include "nara/assets/Image.h"

#include "assets/Inflate.h"
#include "nara/core/Log.h"

#include <cstdlib> // std::abs
#include <cstring>
#include <fstream>

namespace nara {
namespace {

uint32_t ReadU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

int PaethPredictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

// Bytes per pixel for a given PNG color type at bit depth 8.
int ChannelsForColorType(int colorType) {
    switch (colorType) {
        case 0: return 1; // grayscale
        case 2: return 3; // RGB
        case 3: return 1; // palette index
        case 4: return 2; // grayscale + alpha
        case 6: return 4; // RGBA
        default: return 0;
    }
}

} // namespace

Image DecodePNG(const uint8_t* data, size_t size) {
    Image image;

    static const uint8_t kSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 8 || std::memcmp(data, kSignature, 8) != 0) {
        NARA_ERROR("PNG: bad signature");
        return image;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    int bitDepth = 0;
    int colorType = 0;
    int interlace = 0;
    std::vector<uint8_t> palette;     // RGB triples
    std::vector<uint8_t> paletteAlpha; // optional, one per palette entry
    std::vector<uint8_t> idat;        // concatenated compressed data

    size_t offset = 8;
    bool sawIHDR = false;
    while (offset + 8 <= size) {
        const uint32_t chunkLen = ReadU32BE(data + offset);
        const uint8_t* type = data + offset + 4;
        const uint8_t* chunk = data + offset + 8;
        if (offset + 12 + chunkLen > size) {
            NARA_ERROR("PNG: truncated chunk");
            return image;
        }

        if (std::memcmp(type, "IHDR", 4) == 0) {
            width = ReadU32BE(chunk);
            height = ReadU32BE(chunk + 4);
            bitDepth = chunk[8];
            colorType = chunk[9];
            interlace = chunk[12];
            sawIHDR = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(chunk, chunk + chunkLen);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            if (colorType == 3)
                paletteAlpha.assign(chunk, chunk + chunkLen);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), chunk, chunk + chunkLen);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }

        offset += 12 + chunkLen; // length + type + data + CRC
    }

    if (!sawIHDR || width == 0 || height == 0) {
        NARA_ERROR("PNG: missing or invalid IHDR");
        return image;
    }
    if (bitDepth != 8) {
        NARA_ERROR("PNG: unsupported bit depth %d (only 8 is supported)", bitDepth);
        return image;
    }
    if (interlace != 0) {
        NARA_ERROR("PNG: interlaced images are not supported");
        return image;
    }
    const int channels = ChannelsForColorType(colorType);
    if (channels == 0) {
        NARA_ERROR("PNG: unsupported color type %d", colorType);
        return image;
    }

    // Decompress the zlib stream into filtered scanlines.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + width * channels));
    if (!detail::ZlibInflate(idat.data(), idat.size(), raw)) {
        NARA_ERROR("PNG: zlib decompression failed");
        return image;
    }

    const size_t stride = static_cast<size_t>(width) * channels;
    if (raw.size() < height * (stride + 1)) {
        NARA_ERROR("PNG: not enough decompressed data");
        return image;
    }

    // Reconstruct scanlines by reversing the per-row filter, then expand to RGBA.
    std::vector<uint8_t> current(stride, 0);
    std::vector<uint8_t> previous(stride, 0);
    image.pixels.resize(static_cast<size_t>(width) * height * 4);

    size_t src = 0;
    for (uint32_t y = 0; y < height; ++y) {
        const int filter = raw[src++];
        for (size_t x = 0; x < stride; ++x) {
            const int rawByte = raw[src++];
            const int a = (x >= static_cast<size_t>(channels)) ? current[x - channels] : 0;
            const int b = previous[x];
            const int c = (x >= static_cast<size_t>(channels)) ? previous[x - channels] : 0;
            int value = rawByte;
            switch (filter) {
                case 0: break;                              // None
                case 1: value += a; break;                  // Sub
                case 2: value += b; break;                  // Up
                case 3: value += (a + b) / 2; break;        // Average
                case 4: value += PaethPredictor(a, b, c); break; // Paeth
                default:
                    NARA_ERROR("PNG: unknown filter %d", filter);
                    return Image{};
            }
            current[x] = static_cast<uint8_t>(value & 0xFF);
        }

        // Expand this scanline to RGBA.
        uint8_t* dst = image.pixels.data() + static_cast<size_t>(y) * width * 4;
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t r = 0, g = 0, bl = 0, al = 255;
            switch (colorType) {
                case 0: r = g = bl = current[x]; break;
                case 2: r = current[x * 3]; g = current[x * 3 + 1]; bl = current[x * 3 + 2]; break;
                case 4: r = g = bl = current[x * 2]; al = current[x * 2 + 1]; break;
                case 6:
                    r = current[x * 4]; g = current[x * 4 + 1];
                    bl = current[x * 4 + 2]; al = current[x * 4 + 3];
                    break;
                case 3: {
                    const size_t idx = current[x];
                    if (idx * 3 + 2 < palette.size()) {
                        r = palette[idx * 3];
                        g = palette[idx * 3 + 1];
                        bl = palette[idx * 3 + 2];
                    }
                    al = idx < paletteAlpha.size() ? paletteAlpha[idx] : 255;
                    break;
                }
            }
            dst[x * 4 + 0] = r;
            dst[x * 4 + 1] = g;
            dst[x * 4 + 2] = bl;
            dst[x * 4 + 3] = al;
        }

        current.swap(previous); // this row becomes the previous row
    }

    image.width = width;
    image.height = height;
    return image;
}

Image LoadImageFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        NARA_ERROR("Image: cannot open '%s'", path.c_str());
        return Image{};
    }
    const std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
        NARA_ERROR("Image: failed to read '%s'", path.c_str());
        return Image{};
    }
    return DecodePNG(bytes.data(), bytes.size());
}

} // namespace nara
