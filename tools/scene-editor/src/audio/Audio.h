#pragma once

#include <string>

namespace SceneEditor {

// XAudio2-backed audio playback (M8c2). Supports multi-
// voice mixing, per-voice volume, and a small in-memory
// WAV cache so re-playing the same SFX doesn't re-load
// from disk. The PlaySound-based M8c1 implementation
// only allowed a single concurrent voice; this version
// can fire dozens of overlapping sounds at the same time.
//
// Lua surface (M8c1):
//   audio.play(path [, volume])  -> bool
//   audio.stop()
//
// `Play` lazily initialises the XAudio2 engine + master
// voice on first call so the editor doesn't pay the
// COM-init cost when no scripts ask for sound.
class Audio {
public:
    // Returns true if the WAV file loaded + the source
    // voice was started. Missing files / unsupported
    // formats fail honestly with `false` -- a behavioural
    // upgrade from the M8c1 PlaySound that returned true
    // even for typo'd paths.
    static bool Play(const std::string& absolutePath,
                     float           volume = 1.0f);
    // Stops all currently-playing source voices + releases
    // them. The cached SoundClip data sticks around; the
    // next Play() restarts cleanly without re-loading.
    static void Stop();
    // Explicit teardown -- normally the static engine is
    // destroyed at program exit, but the editor can call
    // this to release the master voice between project
    // reloads if it cares.
    static void Shutdown();
};

} // namespace SceneEditor
