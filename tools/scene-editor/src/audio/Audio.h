#pragma once

#include <cstdint>
#include <string>

namespace SceneEditor {

// XAudio2-backed audio playback (M8c2). Supports multi-
// voice mixing, per-voice volume, and a small in-memory
// WAV cache so re-playing the same SFX doesn't re-load
// from disk. The PlaySound-based M8c1 implementation
// only allowed a single concurrent voice; this version
// can fire dozens of overlapping sounds at the same time.
//
// Lua surface (M8c1 + M8c5):
//   audio.play(path_or_guid [, volume])  -> bool   -- SFX (M8c2)
//   audio.stop()                                   -- stop all SFX
//   audio.play_music(path [, volume [, loop]])     -- streaming (M8c5)
//   audio.stop_music()                             -- stop the music
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
    // M8c8 3D-positional one-shot. Same lookup + cache as
    // Play(), but applies a stereo pan via SetOutputMatrix
    // for mono sources. `pan` is in [-1, 1]: -1 = full
    // left, 0 = centred, +1 = full right. Stereo sources
    // get the volume but skip the matrix (they already
    // carry their own L/R balance).
    static bool PlayPositional(const std::string& absolutePath,
                                float volume,
                                float pan);
    // Stops all currently-playing SFX source voices.
    static void Stop();
    // Explicit teardown -- normally the static engine is
    // destroyed at program exit, but the editor can call
    // this to release the master voice between project
    // reloads if it cares.
    static void Shutdown();

    // M8c5 streaming music. Decodes chunk-by-chunk into a
    // ring of 3 buffers (~93 ms each at 44.1 kHz / 4096
    // samples) so a multi-minute OGG / WAV doesn't blow up
    // memory the way the M8c2 cache would. One slot only;
    // a second `PlayMusic` call cleanly stops the previous
    // one. Returns false on missing file / unsupported
    // format (WAV + OGG supported today; MP3 streaming
    // lands when a frame-by-frame decoder loop is wired
    // through minimp3_ex).
    static bool PlayMusic(const std::string& absolutePath,
                          float volume = 1.0f,
                          bool  loop   = false);
    // Stops the active music stream + releases the voice
    // + closes the decoder handle.
    static void StopMusic();
    // Called per frame from the editor's main loop -- tops
    // up the buffer queue so the voice never starves. The
    // pre-fill at PlayMusic time gives ~280 ms of slack
    // before this matters, so it's safe to call once per
    // render frame even at 30 Hz.
    static void UpdateMusic();

    // M8c9 submix bus volumes. Every SFX voice routes
    // through the Sfx submix, every music stream routes
    // through the Music submix, both submixes feed the
    // mastering voice. Per-bus volume composes
    // multiplicatively with the per-voice volume the
    // caller passes to Play / PlayMusic, so a Lua slider
    // can fade just SFX without touching music.
    //
    // Master volume is applied to the mastering voice
    // directly; everything ends up scaled by it.
    // GetBusVolume returns the last value set (or the
    // default of 1.0); calling it before Initialize is
    // safe.
    enum class Bus { Master, Sfx, Music };
    static void  SetBusVolume(Bus bus, float volume);
    static float GetBusVolume(Bus bus);

    // M8c11 per-voice control. Spawn() reserves one slot of
    // a 64-voice pool + returns an opaque handle the caller
    // can use to stop / pause / resume / re-volume the voice
    // until it finishes naturally. Finished voices recycle
    // their slot on the next Spawn(); the handle's high
    // bits carry a generation counter so a stale handle
    // can't accidentally control a re-used slot. Returns 0
    // on missing file / full pool / unsupported format.
    using VoiceHandle = std::uint32_t;
    static VoiceHandle Spawn(const std::string& absolutePath,
                              float volume = 1.0f);
    static bool StopVoice(VoiceHandle h);
    static bool PauseVoice(VoiceHandle h);
    static bool ResumeVoice(VoiceHandle h);
    static bool SetVoiceVolume(VoiceHandle h, float volume);
    static bool IsVoicePlaying(VoiceHandle h);
};

} // namespace SceneEditor
