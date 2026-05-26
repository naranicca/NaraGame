#include "NaraGame/audio/AudioDevice.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)
    #include "audio/xaudio2/XAudio2Device.h"
#endif

namespace NaraGame {

// Silent fallback used when no audio backend is available.
class NullAudioDevice final : public AudioDevice {
public:
    void PlaySound(const AudioClip&, float) override {}
    void PlayMusic(const AudioClip&, float, bool) override {}
    void StopMusic() override {}
    void SetMasterVolume(float) override {}
    void Update() override {}
    const char* BackendName() const override { return "Null (silent)"; }
};

std::unique_ptr<AudioDevice> AudioDevice::Create() {
#if defined(NARAGAME_PLATFORM_WINDOWS)
    auto device = std::make_unique<XAudio2Device>();
    if (device->Valid())
        return device;
    NARAGAME_WARN("XAudio2 unavailable; falling back to silent audio");
#endif
    return std::make_unique<NullAudioDevice>();
}

} // namespace NaraGame
