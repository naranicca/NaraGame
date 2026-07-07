#pragma once

// Include this header in exactly ONE translation unit of a game. It provides
// main() and bootstraps the Application returned by NaraGame::CreateApplication().

#include "NaraGame/core/Application.h"
#include "NaraGame/core/Log.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  include <windows.h>
#  include <cstdio>
#  include <cstdint>
#  include <fcntl.h>
#  include <io.h>
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
    // GUI-subsystem executables don't get a console. Double-click from
    // Explorer -> no window pops up (that's the goal). But we still
    // want `game.exe --build-game X > log.txt` to work when launched
    // from a terminal or driven by a subprocess. The CRT's stdio isn't
    // wired to the inherited handles automatically in this subsystem,
    // so we have to do it by hand:
    //   * If the parent handed us stdio handles (pipes, files, or a
    //     console it owns), _open_osfhandle + _dup2 point the CRT
    //     stdout/stderr at them.
    //   * Otherwise (double-click), AttachConsole tries the parent's
    //     console -- silently no-ops when there isn't one.
    // Both stdout and stderr get their buffering disabled so log lines
    // land promptly in whatever consumer is reading them.
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
        const bool haveOut = (hOut != nullptr && hOut != INVALID_HANDLE_VALUE);
        const bool haveErr = (hErr != nullptr && hErr != INVALID_HANDLE_VALUE);
        if (!haveOut && !haveErr) {
            if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                FILE* fp = nullptr;
                (void)freopen_s(&fp, "CONOUT$", "w", stdout);
                (void)freopen_s(&fp, "CONOUT$", "w", stderr);
                (void)freopen_s(&fp, "CONIN$",  "r", stdin);
            }
        } else {
            if (haveOut) {
                int fd = _open_osfhandle((intptr_t)hOut, _O_TEXT);
                if (fd >= 0) _dup2(fd, _fileno(stdout));
            }
            if (haveErr) {
                int fd = _open_osfhandle((intptr_t)hErr, _O_TEXT);
                if (fd >= 0) _dup2(fd, _fileno(stderr));
            }
        }
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
#endif
    NaraGame::Log::Init();

    NaraGame::Application* app = NaraGame::CreateApplication(argc, argv);
    if (app == nullptr) {
        NARAGAME_ERROR("CreateApplication() returned null");
        NaraGame::Log::Shutdown();
        return 1;
    }

    app->Run();
    delete app;

    NaraGame::Log::Shutdown();
    return 0;
}
