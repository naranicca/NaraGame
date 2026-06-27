#include "ScriptHost.h"

#include "../scene/AnimatorComponent.h"
#include "../scene/Entity.h"
#include "../scene/Scene.h"

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

// Resolve the host pointer stashed as the closure upvalue. Used by
// every animator API callback to find its way back to the C++
// world.
static ScriptHost* HostFromUpvalue(lua_State* L) {
    return static_cast<ScriptHost*>(
        lua_touserdata(L, lua_upvalueindex(1)));
}

// Walks the bound scene + returns the AnimatorComponent attached
// to the named entity, or nullptr when either the entity or the
// component is missing. Scripts that drive a missing entity just
// silently no-op.
static AnimatorComponent* FindAnimator(ScriptHost* host,
                                       const char* entityName) {
    if (!host || !host->BoundScene() || !entityName) return nullptr;
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) return nullptr;
    return e->GetComponent<AnimatorComponent>();
}

// animator.set_bool / get_bool / set_float / get_float / set_int /
// get_int / set_trigger / set_vec2 / get_vec2 / set_color /
// get_color / set_string / get_string. Setters return nothing;
// getters return the requested value (Vec2 returns x, y -- Color
// returns r, g, b, a -- in stack order). Missing entities + missing
// AnimatorComponents are silent no-ops; missing parameter entries
// fall back to a zero / empty default on read.
static int LuaAnimatorSetBool(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramBools[luaL_checkstring(L, 2)] = lua_toboolean(L, 3) != 0;
    return 0;
}
static int LuaAnimatorGetBool(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushboolean(L, 0); return 1; }
    auto it = an->paramBools.find(luaL_checkstring(L, 2));
    lua_pushboolean(L, it != an->paramBools.end() && it->second);
    return 1;
}
static int LuaAnimatorSetFloat(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramFloats[luaL_checkstring(L, 2)] =
        float(luaL_checknumber(L, 3));
    return 0;
}
static int LuaAnimatorGetFloat(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushnumber(L, 0.0); return 1; }
    auto it = an->paramFloats.find(luaL_checkstring(L, 2));
    lua_pushnumber(L,
        it != an->paramFloats.end() ? it->second : 0.0f);
    return 1;
}
static int LuaAnimatorSetInt(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramInts[luaL_checkstring(L, 2)] =
        int(luaL_checkinteger(L, 3));
    return 0;
}
static int LuaAnimatorGetInt(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushinteger(L, 0); return 1; }
    auto it = an->paramInts.find(luaL_checkstring(L, 2));
    lua_pushinteger(L,
        it != an->paramInts.end() ? it->second : 0);
    return 1;
}
static int LuaAnimatorSetTrigger(lua_State* L) {
    // Pulses the trigger -- the M5c10 controller runtime
    // auto-resets it to false the moment the firing transition
    // consumes it.
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramBools[luaL_checkstring(L, 2)] = true;
    return 0;
}
static int LuaAnimatorSetVec2(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    NaraGame::Vec2 v{
        float(luaL_checknumber(L, 3)),
        float(luaL_checknumber(L, 4))};
    an->paramVec2[luaL_checkstring(L, 2)] = v;
    return 0;
}
static int LuaAnimatorGetVec2(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0); return 2; }
    auto it = an->paramVec2.find(luaL_checkstring(L, 2));
    if (it == an->paramVec2.end()) {
        lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0);
    } else {
        lua_pushnumber(L, it->second.x);
        lua_pushnumber(L, it->second.y);
    }
    return 2;
}
static int LuaAnimatorSetColor(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    NaraGame::Color c{
        float(luaL_checknumber(L, 3)),
        float(luaL_checknumber(L, 4)),
        float(luaL_checknumber(L, 5)),
        float(luaL_checknumber(L, 6))};
    an->paramColors[luaL_checkstring(L, 2)] = c;
    return 0;
}
static int LuaAnimatorGetColor(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) {
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
        return 4;
    }
    auto it = an->paramColors.find(luaL_checkstring(L, 2));
    if (it == an->paramColors.end()) {
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
    } else {
        lua_pushnumber(L, it->second.r);
        lua_pushnumber(L, it->second.g);
        lua_pushnumber(L, it->second.b);
        lua_pushnumber(L, it->second.a);
    }
    return 4;
}
static int LuaAnimatorSetString(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramStrings[luaL_checkstring(L, 2)] =
        luaL_checkstring(L, 3);
    return 0;
}
static int LuaAnimatorGetString(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushstring(L, ""); return 1; }
    auto it = an->paramStrings.find(luaL_checkstring(L, 2));
    lua_pushstring(L,
        it != an->paramStrings.end() ? it->second.c_str() : "");
    return 1;
}

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
    // Register the `animator` table. Every callback shares the
    // same `this` pointer as a closure upvalue so the C side can
    // find its way back to the bound scene without globals.
    static const luaL_Reg kAnimatorApi[] = {
        {"set_bool",    LuaAnimatorSetBool},
        {"get_bool",    LuaAnimatorGetBool},
        {"set_float",   LuaAnimatorSetFloat},
        {"get_float",   LuaAnimatorGetFloat},
        {"set_int",     LuaAnimatorSetInt},
        {"get_int",     LuaAnimatorGetInt},
        {"set_trigger", LuaAnimatorSetTrigger},
        {"set_vec2",    LuaAnimatorSetVec2},
        {"get_vec2",    LuaAnimatorGetVec2},
        {"set_color",   LuaAnimatorSetColor},
        {"get_color",   LuaAnimatorGetColor},
        {"set_string",  LuaAnimatorSetString},
        {"get_string",  LuaAnimatorGetString},
        {nullptr,       nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kAnimatorApi, 1);
    lua_setglobal(L, "animator");
    if (luaL_dostring(L, kDefaultBootstrap) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine(std::string("[lua-error] bootstrap: ") +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
}

// Tries the `<entryFunction>_<suffix>` global first when an entry
// function is set, then falls back to the bare `On<suffix>`
// global. Returns the actual name placed on top of the Lua stack
// (caller is responsible for popping or pcall'ing); returns empty
// when neither candidate is a function (caller must pop the
// trailing nil itself).
static std::string PushLifecycleHook(lua_State* L,
                                     const std::string& entryFunction,
                                     const char* suffix) {
    if (!entryFunction.empty()) {
        const std::string entryName = entryFunction + "_" + suffix;
        lua_getglobal(L, entryName.c_str());
        if (lua_isfunction(L, -1)) return entryName;
        lua_pop(L, 1);
    }
    const std::string fallback = std::string("On") + suffix;
    lua_getglobal(L, fallback.c_str());
    if (lua_isfunction(L, -1)) return fallback;
    return std::string{};
}

void ScriptHost::CallOnStart(const std::string& entityName,
                             const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "Start");
    if (fn.empty()) { lua_pop(L, 1); return; }
    lua_pushstring(L, entityName.c_str());
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
}

void ScriptHost::CallOnUpdate(const std::string& entityName,
                              float dt,
                              const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "Update");
    if (fn.empty()) { lua_pop(L, 1); return; }
    lua_pushstring(L, entityName.c_str());
    lua_pushnumber(L, dt);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
}

bool ScriptHost::LoadFile(const std::string& absolutePath,
                          const std::string& displayName) {
    if (!m_lua) return false;
    lua_State* L = static_cast<lua_State*>(m_lua);
    if (luaL_loadfile(L, absolutePath.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + displayName + " load: " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + displayName + " run: " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
        return false;
    }
    AppendLine("[lua] loaded " + displayName);
    return true;
}

bool ScriptHost::LoadString(const std::string& code,
                            const std::string& displayName) {
    if (!m_lua) return false;
    lua_State* L = static_cast<lua_State*>(m_lua);
    if (luaL_loadbuffer(L, code.c_str(), code.size(),
                         displayName.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + displayName + " load: " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + displayName + " run: " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
        return false;
    }
    AppendLine("[lua] loaded " + displayName);
    return true;
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
    const AnimationEvent& event,
    const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    // Prefer the per-entity entry function. If it isn't a valid
    // function fall back to the global OnAnimationEvent so the
    // bootstrap still catches the dispatch.
    std::string usedName;
    if (!entryFunction.empty()) {
        lua_getglobal(L, entryFunction.c_str());
        if (lua_isfunction(L, -1)) {
            usedName = entryFunction;
        } else {
            lua_pop(L, 1);
        }
    }
    if (usedName.empty()) {
        lua_getglobal(L, "OnAnimationEvent");
        if (!lua_isfunction(L, -1)) {
            // Tolerate a user script that nuked the global by
            // quietly dropping the dispatch.
            lua_pop(L, 1);
            return;
        }
        usedName = "OnAnimationEvent";
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
        AppendLine("[lua-error] " + usedName + ": " +
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

void ScriptHost::CallOnStart(const std::string&,
                             const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnUpdate(const std::string&, float,
                              const std::string&) { /* stub no-op */ }

bool ScriptHost::LoadFile(const std::string&,
                          const std::string& displayName) {
    AppendLine("[script-stub] would load " + displayName +
               " (vendor Lua to enable)");
    return false;
}

bool ScriptHost::LoadString(const std::string&,
                            const std::string& displayName) {
    AppendLine("[script-stub] would load " + displayName +
               " (vendor Lua to enable)");
    return false;
}

void ScriptHost::OnAnimationEvent(
    const std::string& entityName,
    const std::string& animationName,
    const AnimationEvent& event,
    const std::string& entryFunction) {
    // Stub path: include the entry function in the prefix so the
    // editor still tells the user which fn would have been called
    // when Lua is eventually wired in.
    const std::string fn = entryFunction.empty()
        ? "OnAnimationEvent" : entryFunction;
    const bool hasArgs = !event.stringArg.empty() ||
                         event.floatArg != 0.0f  ||
                         event.intArg   != 0     ||
                         event.boolArg ||
                         event.vec2Arg.x != 0.0f || event.vec2Arg.y != 0.0f ||
                         event.colorArg.a != 0.0f;
    char buf[512];
    if (hasArgs) {
        std::snprintf(buf, sizeof(buf),
                      "[script:%s] %s.%s.%s(\"%s\", %.3f, %d, %s, "
                      "(%.2f,%.2f), (%.2f,%.2f,%.2f,%.2f))",
                      fn.c_str(),
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
                      "[script:%s] %s.%s.%s()",
                      fn.c_str(),
                      entityName.c_str(),
                      animationName.c_str(),
                      event.name.c_str());
    }
    AppendLine(buf);
}

#endif

} // namespace SceneEditor
