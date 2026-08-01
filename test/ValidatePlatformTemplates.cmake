function(require_text file needle)
    file(READ "${file}" content)
    string(FIND "${content}" "${needle}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${file} does not contain required text: ${needle}")
    endif()
endfunction()

set(root "${NARAGAME_SOURCE_DIR}")
require_text("${root}/platform/android/app/build.gradle.kts"
             "naragame.applicationId")
require_text("${root}/platform/android/app/build.gradle.kts"
             "naragame.versionName")
require_text("${root}/platform/android/app/src/main/AndroidManifest.xml"
             "\${appName}")
require_text("${root}/platform/apple/macos/CMakeLists.txt"
             "NaraGameBuildSettings.cmake")
require_text("${root}/platform/apple/ios/CMakeLists.txt"
             "XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER \"\${NARAGAME_APP_ID}\"")
require_text("${root}/tools/editor/src/EditorApp.cpp"
             "m_project.ApplicationId()")
require_text("${root}/engine/src/audio/AudioDevice.cpp"
             "OpenSLAudioDevice")
require_text("${root}/engine/src/audio/AudioDevice.cpp"
             "AppleAudioDevice")
require_text("${root}/.github/workflows/platform-players.yml"
             "gradlew :app:assembleDebug")
require_text("${root}/.github/workflows/platform-players.yml"
             "CMAKE_SYSTEM_NAME=iOS")

message(STATUS "Platform player templates validated")
