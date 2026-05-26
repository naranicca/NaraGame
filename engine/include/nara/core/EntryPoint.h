#pragma once

// Include this header in exactly ONE translation unit of a game. It provides
// main() and bootstraps the Application returned by nara::CreateApplication().

#include "nara/core/Application.h"
#include "nara/core/Log.h"

int main(int argc, char** argv) {
    nara::Log::Init();

    nara::Application* app = nara::CreateApplication(argc, argv);
    if (app == nullptr) {
        NARA_ERROR("CreateApplication() returned null");
        nara::Log::Shutdown();
        return 1;
    }

    app->Run();
    delete app;

    nara::Log::Shutdown();
    return 0;
}
