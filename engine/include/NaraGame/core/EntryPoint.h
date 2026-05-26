#pragma once

// Include this header in exactly ONE translation unit of a game. It provides
// main() and bootstraps the Application returned by NaraGame::CreateApplication().

#include "NaraGame/core/Application.h"
#include "NaraGame/core/Log.h"

int main(int argc, char** argv) {
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
