#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)

#include "NaraGame/audio/AudioDevice.h"

#include <vector>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace NaraGame {

// XAudio2 backend. One mastering voice + a pool of source voices: SFX create a
// transient voice that is reclaimed once its buffer drains; music uses a single
// looping voice.
class XAudio2Device final : public AudioDevice {
public:
    XAudio2Device();
    ~XAudio2Device() override;

    bool Valid() const { return m_xaudio != nullptr && m_master != nullptr; }

    void PlaySound(const AudioClip& clip, float volume) override;
    void PlayMusic(const AudioClip& clip, float volume, bool loop) override;
    void StopMusic() override;
    void SetMasterVolume(float volume) override;
    void Update() override;

    const char* BackendName() const override { return "XAudio2"; }

private:
    IXAudio2SourceVoice* CreateVoice(const AudioClip& clip, bool loop);

    IXAudio2* m_xaudio = nullptr;
    IXAudio2MasteringVoice* m_master = nullptr;
    IXAudio2SourceVoice* m_music = nullptr;
    std::vector<IXAudio2SourceVoice*> m_active;
    bool m_comInitialized = false;
};

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
