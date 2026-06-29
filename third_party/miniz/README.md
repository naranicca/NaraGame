# third_party/miniz

Vendored single-file zlib drop-in the scene-editor uses to compress
asset pak entries. The flag is opt-in: with these files absent the
build still ships (uncompressed paks); drop the two files in to
shrink ~50-70% off typical asset bundles.

| File       | Upstream                                  | License           |
| ---------- | ----------------------------------------- | ----------------- |
| `miniz.h`  | <https://github.com/richgel999/miniz>     | MIT / Public domain |
| `miniz.c`  | <https://github.com/richgel999/miniz>     | MIT / Public domain |

Grab the amalgamated single-file build from a miniz release
(`miniz-x.y.z.zip` → unpack `miniz.h` + `miniz.c`). Both files are
public-domain / MIT so vendoring them in-tree is fine.

The CMake build picks them up automatically:

```cmake
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/miniz/miniz.c)
    target_compile_definitions(scene-editor PRIVATE NARAGAME_HAS_MINIZ=1)
endif()
```

`SceneEditor::WritePak` then `mz_compress2`-s every TOC entry
(skipping the few hundred bytes per file where the deflate header
costs more than it saves). `SceneEditor::ExtractPak` reads the
per-entry `flags` byte + dispatches to `mz_uncompress` for
deflated entries; stored entries take the M9c13 raw-copy path
unchanged.

If miniz isn't vendored, WritePak emits every entry as stored
(NPAK pak v2 with `flags = 0` everywhere). ExtractPak in a binary
without miniz still reads stored entries; encountering a
deflated entry it can't decode logs an error + returns false --
shouldn't happen if writer + reader use the same build, but keeps
the failure mode honest.
