#include "NaraGame/assets/Hdr.h"

#include "NaraGame/core/Log.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace NaraGame {

namespace {

// Convert a 4-byte RGBE pixel to three floats. The RGBE format stores a
// shared exponent E in the alpha slot; the channel value is
// (channel + 0.5) * 2^(E - 128 - 8). E == 0 marks a zero pixel.
void RgbeToFloat3(const uint8_t* rgbe, float* out) {
    if (rgbe[3] == 0) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const float scale = std::ldexp(1.0f, int(rgbe[3]) - (128 + 8));
    out[0] = (float(rgbe[0]) + 0.5f) * scale;
    out[1] = (float(rgbe[1]) + 0.5f) * scale;
    out[2] = (float(rgbe[2]) + 0.5f) * scale;
}

// Decode one new-format RLE scanline. `width` is the scanline length in
// pixels; the four channels are RLE-encoded independently. Returns true
// on success, leaving `outRgbe` filled with width * 4 bytes.
bool DecodeRleScanline(std::ifstream& in, int width,
                       std::vector<uint8_t>& outRgbe) {
    outRgbe.assign(size_t(width) * 4, 0);
    for (int ch = 0; ch < 4; ++ch) {
        int x = 0;
        while (x < width) {
            uint8_t run;
            in.read(reinterpret_cast<char*>(&run), 1);
            if (!in) return false;
            if (run > 128) {
                // Run-length: (run - 128) copies of one byte.
                const int len = run - 128;
                if (x + len > width) return false;
                uint8_t val;
                in.read(reinterpret_cast<char*>(&val), 1);
                if (!in) return false;
                for (int i = 0; i < len; ++i)
                    outRgbe[size_t(x + i) * 4 + ch] = val;
                x += len;
            } else {
                // Raw run: `run` literal bytes.
                const int len = run;
                if (x + len > width) return false;
                for (int i = 0; i < len; ++i) {
                    uint8_t v;
                    in.read(reinterpret_cast<char*>(&v), 1);
                    if (!in) return false;
                    outRgbe[size_t(x + i) * 4 + ch] = v;
                }
                x += len;
            }
        }
    }
    return true;
}

} // namespace

HdrImage LoadHdrFile(const std::string& path) {
    HdrImage img;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        NARAGAME_ERROR("LoadHdrFile: cannot open '%s'", path.c_str());
        return img;
    }

    // ASCII header: starts with "#?RADIANCE" (or "#?RGBE" in older files)
    // followed by metadata lines, an empty line, then a resolution line.
    std::string line;
    if (!std::getline(in, line) ||
        (line.rfind("#?RADIANCE", 0) != 0 && line.rfind("#?RGBE", 0) != 0)) {
        NARAGAME_ERROR("LoadHdrFile: '%s' missing #?RADIANCE magic",
                       path.c_str());
        return img;
    }
    // Skip metadata until blank line.
    while (std::getline(in, line)) {
        if (line.empty()) break;
        // FORMAT, GAMMA, EXPOSURE, etc. -- we ignore all of them.
    }
    // Resolution: "-Y H +X W". The minus sign and order match the
    // standard top-to-bottom layout we want; if a file uses a different
    // orientation we'd need to flip the pixel buffer.
    std::string res;
    if (!std::getline(in, res)) {
        NARAGAME_ERROR("LoadHdrFile: '%s' missing resolution line",
                       path.c_str());
        return img;
    }
    int h = 0, w = 0;
    if (std::sscanf(res.c_str(), "-Y %d +X %d", &h, &w) != 2) {
        NARAGAME_ERROR("LoadHdrFile: '%s' unsupported resolution '%s'",
                       path.c_str(), res.c_str());
        return img;
    }
    if (w <= 0 || h <= 0) {
        NARAGAME_ERROR("LoadHdrFile: '%s' bad dimensions %dx%d",
                       path.c_str(), w, h);
        return img;
    }

    img.width  = uint32_t(w);
    img.height = uint32_t(h);
    img.pixels.assign(size_t(w) * size_t(h) * 3, 0.0f);

    std::vector<uint8_t> scanline(size_t(w) * 4);
    for (int y = 0; y < h; ++y) {
        // Peek the first four bytes of the scanline. If they match the
        // RLE marker (2, 2, hi, lo) with hi&0x80 == 0 and length == w,
        // the scanline is RLE-encoded; otherwise it's raw RGBE.
        uint8_t head[4];
        in.read(reinterpret_cast<char*>(head), 4);
        if (!in) {
            NARAGAME_ERROR("LoadHdrFile: '%s' truncated at scanline %d",
                           path.c_str(), y);
            return HdrImage{};
        }
        if (w >= 8 && head[0] == 2 && head[1] == 2 && (head[2] & 0x80) == 0 &&
            ((int(head[2]) << 8) | int(head[3])) == w) {
            if (!DecodeRleScanline(in, w, scanline)) {
                NARAGAME_ERROR("LoadHdrFile: '%s' RLE decode failed at row %d",
                               path.c_str(), y);
                return HdrImage{};
            }
        } else {
            // Raw scanline: the four head bytes are the first pixel; read
            // (w - 1) more pixels worth of RGBE bytes.
            scanline[0] = head[0];
            scanline[1] = head[1];
            scanline[2] = head[2];
            scanline[3] = head[3];
            if (w > 1) {
                in.read(reinterpret_cast<char*>(scanline.data() + 4),
                        (size_t(w) - 1) * 4);
                if (!in) {
                    NARAGAME_ERROR("LoadHdrFile: '%s' truncated raw at row %d",
                                   path.c_str(), y);
                    return HdrImage{};
                }
            }
        }
        // Decode the scanline's RGBE bytes into float RGB.
        for (int x = 0; x < w; ++x) {
            RgbeToFloat3(scanline.data() + size_t(x) * 4,
                         img.pixels.data() + (size_t(y) * w + size_t(x)) * 3);
        }
    }

    NARAGAME_INFO("LoadHdrFile: '%s' decoded %ux%u", path.c_str(),
                  img.width, img.height);
    return img;
}

} // namespace NaraGame
