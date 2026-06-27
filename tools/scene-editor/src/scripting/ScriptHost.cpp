#include "ScriptHost.h"

#include <cstdio>
#include <string>

#if defined(NARAGAME_SCENE_EDITOR_HAS_LUA) && NARAGAME_SCENE_EDITOR_HAS_LUA
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#endif

namespace SceneEditor {

void ScriptHost::AppendLine(const std::string& line) {
    m_outputLog.push_back(line);
    if (m_outputLog.size() > kOutputLogMax) {
        m_outputLog.erase(
            m_outputLog.begin(),
            m_outputLog.begin() +
                (m_outputLog.size() - kOutputLogMax));
    }
}

#if defined(NARAGAME_SCENE_EDITOR_HAS_LUA) && NARAGAME_SCENE_EDITOR_HAS_LUA
// -----------------------------------------------------------------
// Lua-backed implementation. The interpreter ships with a built-in
// default `OnAnimationEvent` handler so the editor reads the same
// "function call" lines whether or not the user has authored a
// custom script yet.
// -----------------------------------------------------------------

// `print(...)` in script land lands here. Concatenates the args
// like the stock Lua print does + pushes the joined line into the
// host's output log.
static int LuaPrintRedirect(lua_State* L) {
    ScriptHost* host = static_cast<ScriptHost*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!host) return 0;
    const int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) line += '\t';
        line.append(s, len);
        lua_pop(L, 1);
    }
    host->AppendLine(line);
    return 0;
}

// Default OnAnimationEvent handler. Mirrors the M6c1 stub format so
// the Console pane reads the same with or without a vendored Lua;
// user scripts can later overwrite this global with their own
// implementation.
static const char* kDefaultBootstrap =
    "function OnAnimationEvent(entity, anim, event,\n"
    "                          s, f, i, b,\n"
    "                          vx, vy, r, g, bl, a)\n"
    "    local has_args = (s ~= '' or f ~= 0 or i ~= 0 or b\n"
    "                      or vx ~= 0 or vy ~= 0 or a ~= 0)\n"
    "    if has_args then\n"
    "        print(string.format(\n"
    "            '[lua] %s.%s.%s(\\\"%s\\\", %.3f, %d, %s, "
                "(%.2f,%.2f), (%.2f,%.2f,%.2f,%.2f))',\n"
    "            entity, anim, event, s, f, i, tostring(b),\n"
    "            vx, vy, r, g, bl, a))\n"
    "    else\n"
    "        print(string.format('[lua] %s.%s.%s()',\n"
    "            entity, anim, event))\n"
    "    end\n"
    "end\n";

ScriptHost::ScriptHost() {
    lua_State* L = luaL_newstate();
    if (!L) return;
    m_lua = L;
    luaL_openlibs(L);
    // Replace print with our log-redirector. The host pointer
    // travels as an upvalue so the C callback can find us.
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, LuaPrintRedirect, 1);
    lua_setglobal(L, "print");
    if (luaL_dostring(L, kDefaultBootstrap) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine(std::string("[lua-error] bootstrap: ") +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
}

ScriptHost::~ScriptHost() {
    if (m_lua) {
        lua_close(static_cast<lua_State*>(m_lua));
        m_lua = nullptr;
    }
}

void ScriptHost::OnAnimationEvent(
    const std::string& entityName,
    const std::string& animationName,
    const AnimationEvent& event) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    lua_getglobal(L, "OnAnimationEvent");
    if (!lua_isfunction(L, -1)) {
        // Bootstrap should have installed one; tolerate a user
        // script that nuked the global by quietly dropping the
        // dispatch.
        lua_pop(L, 1);
        return;
    }
    lua_pushstring (L, entityName.c_str());
    lua_pushstring (L, animationName.c_str());
    lua_pushstring (L, event.name.c_str());
    lua_pushstring (L, event.stringArg.c_str());
    lua_pushnumber (L, event.floatArg);
    lua_pushinteger(L, event.intArg);
    lua_pushboolean(L, event.boolArg ? 1 : 0);
    lua_pushnumber (L, event.vec2Arg.x);
    lua_pushnumber (L, event.vec2Arg.y);
    lua_pushnumber (L, event.colorArg.r);
    lua_pushnumber (L, event.colorArg.g);
    lua_pushnumber (L, event.colorArg.b);
    lua_pushnumber (L, event.colorArg.a);
    if (lua_pcall(L, 13, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine(std::string("[lua-error] OnAnimationEvent: ") +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
}

#else
// -----------------------------------------------------------------
// Stub fallback (M6c1). Used when third_party/lua isn't vendored.
// Formats the dispatch as a `[script]` "function call" string so
// the Console pane still shows something meaningful.
// -----------------------------------------------------------------

ScriptHost::ScriptHost()  = default;
ScriptHost::~ScriptHost() = default;

void ScriptHost::OnAnimationEvent(
    const std::string& entityName,
    const std::string& animationName,
    const AnimationEvent& event) {
    const bool hasArgs = !event.stringArg.empty() ||
                         event.floatArg != 0.0f  ||
                         event.intArg   != 0     ||
                         event.boolArg ||
                         event.vec2Arg.x != 0.0f || event.vec2Arg.y != 0.0f ||
                         event.colorArg.a != 0.0f;
    char buf[512];
    if (hasArgs) {
        std::snprintf(buf, sizeof(buf),
                      "[script] %s.%s.%s(\"%s\", %.3f, %d, %s, "
                      "(%.2f,%.2f), (%.2f,%.2f,%.2f,%.2f))",
                      entityName.c_str(),
                      animationName.c_str(),
                      event.name.c_str(),
                      event.stringArg.c_str(),
                      double(event.floatArg),
                      event.intArg,
                      event.boolArg ? "true" : "false",
                      double(event.vec2Arg.x),
                      double(event.vec2Arg.y),
                      double(event.colorArg.r),
                      double(event.colorArg.g),
                      double(event.colorArg.b),
                      double(event.colorArg.a));
    } else {
        std::snprintf(buf, sizeof(buf),
                      "[script] %s.%s.%s()",
                      entityName.c_str(),
                      animationName.c_str(),
                      event.name.c_str());
    }
    AppendLine(buf);
}

#endif

} // namespace SceneEditor
