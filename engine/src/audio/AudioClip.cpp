#include "nara/audio/AudioClip.h"

#include "nara/core/Log.h"

#include <cstring>
#include <fstream>

namespace nara {
namespace {

uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

AudioClip LoadWav(const std::string& path) {
    AudioClip clip;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        NARA_ERROR("LoadWav: cannot open '%s'", path.c_str());
        return clip;
    }
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size) || bytes.size() < 12) {
        NARA_ERROR("LoadWav: failed to read '%s'", path.c_str());
        return clip;
    }

    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        NARA_ERROR("LoadWav: '%s' is not a RIFF/WAVE file", path.c_str());
        return clip;
    }

    bool haveFmt = false;
    size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const uint8_t* id = bytes.data() + offset;
        const uint32_t chunkSize = ReadU32LE(bytes.data() + offset + 4);
        const size_t body = offset + 8;
        if (body + chunkSize > bytes.size())
            break;

        if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
            const uint16_t format = ReadU16LE(bytes.data() + body);
            clip.channels = ReadU16LE(bytes.data() + body + 2);
            clip.sampleRate = ReadU32LE(bytes.data() + body + 4);
            clip.bitsPerSample = ReadU16LE(bytes.data() + body + 14);
            if (format != 1) { // 1 = PCM
                NARA_ERROR("LoadWav: '%s' is not PCM (format %u)", path.c_str(), format);
                return AudioClip{};
            }
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            clip.data.assign(bytes.begin() + body, bytes.begin() + body + chunkSize);
        }

        offset = body + chunkSize + (chunkSize & 1u); // chunks are word-aligned
    }

    if (!haveFmt || !clip.Valid()) {
        NARA_ERROR("LoadWav: '%s' missing fmt/data", path.c_str());
        return AudioClip{};
    }

    NARA_INFO("Loaded WAV '%s' (%u Hz, %u ch, %u-bit, %.2fs)", path.c_str(), clip.sampleRate,
              clip.channels, clip.bitsPerSample, clip.DurationSeconds());
    return clip;
}

} // namespace nara
