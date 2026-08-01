#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_ANDROID)

#include "NaraGame/audio/AudioDevice.h"

#include <SLES/OpenSLES.h>

#include <memory>
#include <vector>

namespace NaraGame {

class OpenSLAudioDevice final : public AudioDevice {
public:
    struct Voice;
    OpenSLAudioDevice();
    ~OpenSLAudioDevice() override;

    bool Valid() const { return m_engineObject != nullptr && m_outputMix != nullptr; }
    void PlaySound(const AudioClip&, float volume) override;
    void PlayMusic(const AudioClip&, float volume, bool loop) override;
    void StopMusic() override;
    void SetMasterVolume(float volume) override;
    void Update() override;
    const char* BackendName() const override { return "OpenSL ES"; }

private:
    std::unique_ptr<Voice> CreateVoice(const AudioClip&, float, bool);

    SLObjectItf m_engineObject = nullptr;
    SLEngineItf m_engine = nullptr;
    SLObjectItf m_outputMix = nullptr;
    std::vector<std::unique_ptr<Voice>> m_active;
    std::unique_ptr<Voice> m_music;
    float m_masterVolume = 1.0f;
};

} // namespace NaraGame

#endif
