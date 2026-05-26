#pragma once

#include "NaraGame/audio/AudioClip.h"

#include <memory>

namespace NaraGame {

// Audio output abstraction. Create() always returns a usable device — a real
// backend (XAudio2 on Windows) when available, otherwise a silent no-op device,
// so game code never has to null-check.
class AudioDevice {
public:
    static std::unique_ptr<AudioDevice> Create();

    virtual ~AudioDevice() = default;

    // Fire-and-forget sound effect (mixed with others).
    virtual void PlaySound(const AudioClip& clip, float volume = 1.0f) = 0;

    // Background music on a dedicated channel; replaces any current music.
    virtual void PlayMusic(const AudioClip& clip, float volume = 1.0f, bool loop = true) = 0;
    virtual void StopMusic() = 0;

    virtual void SetMasterVolume(float volume) = 0;

    // Called once per frame by the engine to reclaim finished voices.
    virtual void Update() = 0;

    virtual const char* BackendName() const = 0;
};

} // namespace NaraGame
