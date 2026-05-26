#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nara {

// Decoded PCM audio held in memory. The bytes stay valid for the clip's whole
// lifetime, so a clip must outlive any sound playing from it.
struct AudioClip {
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<uint8_t> data; // interleaved PCM frames

    bool Valid() const { return sampleRate > 0 && channels > 0 && !data.empty(); }

    float DurationSeconds() const {
        const uint32_t bytesPerSec = sampleRate * channels * (bitsPerSample / 8u);
        return bytesPerSec ? static_cast<float>(data.size()) / bytesPerSec : 0.0f;
    }
};

// Load a PCM .wav file (from-scratch RIFF parser, 8/16-bit). Invalid on failure.
AudioClip LoadWav(const std::string& path);

} // namespace nara
