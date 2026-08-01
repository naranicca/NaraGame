#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)

#include "NaraGame/audio/AudioDevice.h"

#include <memory>
#include <vector>

namespace NaraGame {

class AppleAudioDevice final : public AudioDevice {
public:
    struct ActiveVoice;

    AppleAudioDevice();
    ~AppleAudioDevice() override;
    bool Valid() const;

    void PlaySound(const AudioClip&, float volume) override;
    void PlayMusic(const AudioClip&, float volume, bool loop) override;
    void StopMusic() override;
    void SetMasterVolume(float volume) override;
    void Update() override;
    const char* BackendName() const override { return "AVAudioEngine"; }

private:
    std::unique_ptr<ActiveVoice> CreateVoice(const AudioClip&, float, bool);
    void DestroyVoice(std::unique_ptr<ActiveVoice>&);

    void* m_engine = nullptr;
    std::vector<std::unique_ptr<ActiveVoice>> m_active;
    std::unique_ptr<ActiveVoice> m_music;
};

} // namespace NaraGame

#endif
