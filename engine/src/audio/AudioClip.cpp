#include "NaraGame/audio/AudioClip.h"

#include "NaraGame/core/Log.h"

#include <cstring>
#include <fstream>

namespace NaraGame {
namespace {

uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

AudioClip LoadWavMemory(const uint8_t* source, size_t sourceSize,
                        const std::string& debugName) {
    AudioClip clip;
    if (source == nullptr || sourceSize < 12) {
        NARAGAME_ERROR("LoadWav: '%s' is too small", debugName.c_str());
        return clip;
    }

    if (std::memcmp(source, "RIFF", 4) != 0 ||
        std::memcmp(source + 8, "WAVE", 4) != 0) {
        NARAGAME_ERROR("LoadWav: '%s' is not a RIFF/WAVE file", debugName.c_str());
        return clip;
    }

    bool haveFmt = false;
    size_t offset = 12;
    while (offset + 8 <= sourceSize) {
        const uint8_t* id = source + offset;
        const uint32_t chunkSize = ReadU32LE(source + offset + 4);
        const size_t body = offset + 8;
        if (body + chunkSize > sourceSize)
            break;

        if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
            const uint16_t format = ReadU16LE(source + body);
            clip.channels = ReadU16LE(source + body + 2);
            clip.sampleRate = ReadU32LE(source + body + 4);
            clip.bitsPerSample = ReadU16LE(source + body + 14);
            if (format != 1) { // 1 = PCM
                NARAGAME_ERROR("LoadWav: '%s' is not PCM (format %u)", debugName.c_str(), format);
                return AudioClip{};
            }
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            clip.data.assign(source + body, source + body + chunkSize);
        }

        offset = body + chunkSize + (chunkSize & 1u); // chunks are word-aligned
    }

    if (!haveFmt || !clip.Valid()) {
        NARAGAME_ERROR("LoadWav: '%s' missing fmt/data", debugName.c_str());
        return AudioClip{};
    }

    NARAGAME_INFO("Loaded WAV '%s' (%u Hz, %u ch, %u-bit, %.2fs)", debugName.c_str(), clip.sampleRate,
              clip.channels, clip.bitsPerSample, clip.DurationSeconds());
    return clip;
}

AudioClip LoadWav(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        NARAGAME_ERROR("LoadWav: cannot open '%s'", path.c_str());
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) return {};
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        NARAGAME_ERROR("LoadWav: failed to read '%s'", path.c_str());
        return {};
    }
    return LoadWavMemory(bytes.data(), bytes.size(), path);
}

} // namespace NaraGame
