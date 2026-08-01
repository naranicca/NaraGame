#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGamePlayer/ProjectPlayer.h>

#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <filesystem>
#include <fstream>
#include <vector>

namespace NaraGame {
android_app* GetAndroidApp();
}

namespace {

bool CopyAsset(AAssetManager* manager, const char* assetPath,
               const std::filesystem::path& destination) {
    AAsset* asset = AAssetManager_open(manager, assetPath,
                                       AASSET_MODE_STREAMING);
    if (asset == nullptr) return false;
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream output(destination, std::ios::binary);
    std::vector<char> buffer(64 * 1024);
    int count = 0;
    while ((count = AAsset_read(asset, buffer.data(), buffer.size())) > 0)
        output.write(buffer.data(), count);
    AAsset_close(asset);
    return output.good();
}

std::string StageBundledProject() {
    android_app* nativeApp = NaraGame::GetAndroidApp();
    if (nativeApp == nullptr || nativeApp->activity == nullptr) return {};
    const std::filesystem::path root = nativeApp->activity->internalDataPath;
    std::error_code ec;
    std::filesystem::remove_all(root / "Assets", ec);
    CopyAsset(nativeApp->activity->assetManager, "assets.pak",
              root / "assets.pak");
    CopyAsset(nativeApp->activity->assetManager,
              "ProjectSettings/project.json",
              root / "ProjectSettings" / "project.json");
    return root.string();
}
}

NaraGame::Application* NaraGame::CreateApplication(int, char**) {
    auto app = NaraGamePlayer::CreateProjectPlayer({StageBundledProject(), 0});
    return app.release();
}
