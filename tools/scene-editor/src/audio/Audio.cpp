#include "Audio.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

namespace SceneEditor {

bool Audio::Play(const std::string& absolutePath) {
    if (absolutePath.empty()) return false;
    const DWORD flags = SND_ASYNC | SND_FILENAME | SND_NODEFAULT;
    return PlaySoundA(absolutePath.c_str(), nullptr, flags) != FALSE;
}

void Audio::Stop() {
    PlaySoundA(nullptr, nullptr, 0);
}

} // namespace SceneEditor
