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
};

} // namespace SceneEditor
