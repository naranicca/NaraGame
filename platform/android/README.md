# NaraGame Android runtime

This is the NativeActivity/Gradle host used by the editor's Android build.

Requirements:

- JDK 17
- Android SDK 37
- Android NDK `28.2.13676358`
- CMake 3.22.1 or newer
- Gradle 9.5 (or a compatible Android Studio installation)

Open this directory in Android Studio, or configure a Gradle wrapper once when
Gradle is installed:

```text
gradle wrapper --gradle-version 9.5.0
```

Then build with `gradlew.bat :app:assembleRelease` on Windows or
`./gradlew :app:assembleRelease` on macOS/Linux.

The current player validates the NativeActivity window, Android input, and
Vulkan runtime path. Loading editor-authored `.nscene` projects is the next
runtime-extraction milestone; those systems currently live in the Windows
editor target rather than in the platform-neutral engine library.

An editor export places `assets.pak` and `ProjectSettings/` in
`app/src/main/assets`; Gradle packages both into the APK/AAB automatically.
