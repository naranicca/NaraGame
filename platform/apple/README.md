# NaraGame Apple runtime foundations

These templates require macOS with Xcode and currently validate the native
Cocoa/UIKit + CAMetalLayer + Metal clear/present path.

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
Metal resource objects and the editor-authored scene player still need to be
extracted from the Windows editor before full games can render on Apple
platforms; the present templates intentionally render a clear screen rather
than reporting an incomplete game package as successful.
