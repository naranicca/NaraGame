# Lua 5.4 (vendored)

The scene-editor's `ScriptHost` switches from a stub to a real Lua-backed
implementation when this folder contains the Lua 5.4 sources. Until you
drop them in, the editor still builds; `ScriptHost::OnAnimationEvent`
just routes through the M6c1 stub that formats events into the Console
panel's Script Output pane.

## How to vendor

1. Grab the Lua 5.4 source archive (e.g. `lua-5.4.7.tar.gz`) from
   <https://www.lua.org/ftp/>.
2. Extract the contents of `src/` directly into this folder. The
   archive's `src/Makefile`, `src/lua.c` (standalone interpreter
   main), and `src/luac.c` (compiler tool) are **not** needed -- the
   CMake target lists only the library sources.
3. Re-run CMake. It detects `third_party/lua/lua.h` + builds a small
   `lua` static lib + defines `NARAGAME_SCENE_EDITOR_HAS_LUA=1` on
   the scene-editor target.

## Files the CMake target expects

Core (`lapi.c`, `lcode.c`, `lctype.c`, `ldebug.c`, `ldo.c`, `ldump.c`,
`lfunc.c`, `lgc.c`, `llex.c`, `lmem.c`, `lobject.c`, `lopcodes.c`,
`lparser.c`, `lstate.c`, `lstring.c`, `ltable.c`, `ltm.c`, `lundump.c`,
`lvm.c`, `lzio.c`) plus the standard libraries (`lauxlib.c`,
`lbaselib.c`, `lcorolib.c`, `ldblib.c`, `linit.c`, `liolib.c`,
`lmathlib.c`, `loadlib.c`, `loslib.c`, `lstrlib.c`, `ltablib.c`,
`lutf8lib.c`).

Headers stay alongside the sources -- the CMake target adds this
folder to the `lua` library's PUBLIC include path so the rest of the
editor's includes (`extern "C" { #include <lua.h> }` etc.) just work.

## License

Lua is released under the MIT license. The license text lives in the
top of `lua.h` -- preserve it when vendoring.
