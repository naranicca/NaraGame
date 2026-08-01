#pragma once

#include <memory>
#include <string>

namespace NaraGame {
class Application;
}

namespace NaraGamePlayer {

struct LaunchOptions {
    std::string projectRoot;
    int maxFrames = 0;
};

// Creates the platform-neutral project runtime. Native hosts own the returned
// Application and drive it through Application::Run().
std::unique_ptr<NaraGame::Application> CreateProjectPlayer(
    LaunchOptions options);

// Desktop command-line parser. With no --project argument it mounts an
// assets.pak next to the executable and uses that directory as the project.
LaunchOptions ParseLaunchOptions(int argc, char** argv);

} // namespace NaraGamePlayer
