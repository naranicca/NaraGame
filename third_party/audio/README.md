# third_party/audio

Vendored single-file audio decoders the scene-editor uses when
the matching CMake flag is set. All three files are public-
domain so vendoring them in-tree is allowed.

| File             | Format     | Upstream                                           | License |
| ---------------- | ---------- | -------------------------------------------------- | ------- |
| `stb_vorbis.c`   | OGG Vorbis | <https://github.com/nothings/stb>                  | Public domain / MIT |
| `minimp3.h`      | MP3 core   | <https://github.com/lieff/minimp3>                 | CC0 (public domain) |
| `minimp3_ex.h`   | MP3 + file IO helpers | <https://github.com/lieff/minimp3>     | CC0 (public domain) |

The CMake build picks them up automatically:

```cmake
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/audio/stb_vorbis.c)
    target_compile_definitions(scene-editor PRIVATE NARAGAME_HAS_STB_VORBIS=1)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/audio/minimp3.h)
    target_compile_definitions(scene-editor PRIVATE NARAGAME_HAS_MINIMP3=1)
endif()
```

`Audio::Play` dispatches by file extension:

- `.wav` -> minimum-viable RIFF parser (always available)
- `.ogg` -> `stb_vorbis_decode_filename` (when `NARAGAME_HAS_STB_VORBIS`)
- `.mp3` -> `mp3dec_load` (when `NARAGAME_HAS_MINIMP3`)

If a decoder isn't vendored, calling `audio.play("track.ogg")`
returns `false` from Lua rather than crashing. The behaviour
matches the M8c2 honest-failure contract.

## Refreshing the vendored copies

```powershell
$dir = "third_party/audio"
Invoke-WebRequest "https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c" -OutFile "$dir/stb_vorbis.c" -UseBasicParsing
Invoke-WebRequest "https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h" -OutFile "$dir/minimp3.h" -UseBasicParsing
Invoke-WebRequest "https://raw.githubusercontent.com/lieff/minimp3/master/minimp3_ex.h" -OutFile "$dir/minimp3_ex.h" -UseBasicParsing
```

Re-build after refreshing -- the CMake check fires on every
configure step, so a missing file silently turns the
corresponding decoder off + an added file turns it on.
