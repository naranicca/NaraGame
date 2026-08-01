# NaraGame Android player

This is the NativeActivity/Gradle host used by the editor's Android build.

Requirements:

- JDK 17
- Android SDK 37 (`platforms;android-37.0`)
- Android NDK `28.2.13676358`
- CMake 3.22.1 or newer
- Gradle 9.5 (the checked-in wrapper downloads it automatically)

An editor export writes a self-contained native build project under
`platform/android`. Open that directory in Android Studio, or build directly
with `gradlew.bat :app:assembleRelease` on Windows or
`./gradlew :app:assembleRelease` on macOS/Linux.

The NativeActivity host stages `assets.pak` and `ProjectSettings/` from the APK
into app-private storage, then starts the shared `NaraGameProjectRuntime`.
Editor-authored scenes, Lua scripts, sprites, tilemaps, animation, physics,
and Android input use the same runtime code as the desktop player. PCM WAV
sound effects and music are output through the native OpenSL ES backend.

The Vulkan renderer is used on Android. An editor export places `assets.pak`
and `ProjectSettings/` in `app/src/main/assets`; Gradle packages both into the
APK/AAB automatically. Device installation and store signing remain native
Android deployment steps.
