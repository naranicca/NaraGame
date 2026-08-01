# NaraGame Apple players

These templates require macOS with Xcode. Both hosts create a native
Cocoa/UIKit window backed by `CAMetalLayer`, stage the bundled game payload,
and start the shared `NaraGameProjectRuntime`.

macOS:

```sh
cmake -S platform/apple/macos -B build/macos -G Xcode
cmake --build build/macos --config Release
```

iPhone/iPad simulator or device project:

```sh
cmake -S platform/apple/ios -B build/ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS
cmake --build build/ios --config Release
```

Device archives require an Apple Developer team and code-signing settings.
An editor export places `assets.pak` and `ProjectSettings/` under each
platform's `Resources/` directory. CMake adds those files to the generated app
bundle automatically.

The Metal backend supports the 2D sprite, MSDF text, fog-sprite, texture,
buffer, blend, and render-target paths used by the project player. Full 3D,
depth, and HDR post-processing parity is still future work. Because the Apple
sources cannot be compiled on Windows, exported projects must be built and
tested with Xcode on macOS before distribution.

PCM WAV sound effects and music are output through AVAudioEngine on macOS and
iOS. OGG/MP3 decoding and per-voice pause/resume parity remain future work on
the portable player audio path.
