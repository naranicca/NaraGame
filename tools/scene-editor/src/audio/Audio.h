#pragma once

#include <string>

namespace SceneEditor {

// Minimum-viable audio playback (M8c1). Wraps the Windows
// PlaySound() API to fire a WAV file off the disk without
// blocking the main thread. One simultaneous voice is the
// PlaySound limitation; a later M8 commit can swap this for
// XAudio2 or a vendored mixer if the project needs layered
// SFX + music, without changing the Lua-facing surface.
//
// Lua surface:
//   audio.play(path)  -> bool   -- absolute path or path
//                                  relative to the project
//                                  Assets root.
//   audio.stop()              -- silences the active voice.
class Audio {
public:
    // Returns true if PlaySound accepted the request.
    // The actual file load + playback continues asynchronously;
    // a missing or malformed WAV silently fails to make sound
    // but still returns true from PlaySound on some Windows
    // builds, so the caller can't rely on the bool for
    // file-existence checks.
    static bool Play(const std::string& absolutePath);
    static void Stop();
};

} // namespace SceneEditor
