#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGamePlayer/ProjectPlayer.h>

NaraGame::Application* NaraGame::CreateApplication(int argc, char** argv) {
    auto application = NaraGamePlayer::CreateProjectPlayer(
        NaraGamePlayer::ParseLaunchOptions(argc, argv));
    return application.release();
}
