#include "NaraGame/audio/AudioDevice.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)
    #include "audio/xaudio2/XAudio2Device.h"
#elif defined(NARAGAME_PLATFORM_ANDROID)
    #include "audio/opensl/OpenSLAudioDevice.h"
#elif defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)
    #include "audio/apple/AppleAudioDevice.h"
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
#elif defined(NARAGAME_PLATFORM_ANDROID)
    auto device = std::make_unique<OpenSLAudioDevice>();
    if (device->Valid()) return device;
    NARAGAME_WARN("OpenSL ES unavailable; falling back to silent audio");
#elif defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)
    auto device = std::make_unique<AppleAudioDevice>();
    if (device->Valid()) return device;
    NARAGAME_WARN("AVAudioEngine unavailable; falling back to silent audio");
#endif
    return std::make_unique<NullAudioDevice>();
}

} // namespace NaraGame
