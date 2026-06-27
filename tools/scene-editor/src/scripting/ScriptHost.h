#pragma once

#include "../asset/Animation.h"

#include <string>
#include <vector>

namespace SceneEditor {

// Editor-side scripting host. M6c2 wires up a real Lua 5.4
// interpreter when third_party/lua is vendored; when the sources
// aren't in place yet the host falls back to the M6c1 stub that
// only formats events into the Console output pane.
//
// The host's job is to receive engine-side events (animation hooks,
// component lifecycle calls, runtime parameter mutations) and route
// them through whichever script the matching ScriptComponent has
// attached. The Lua-backed implementation registers a global
// `OnAnimationEvent` hook + replaces `print` with a redirector
// that pushes lines into m_outputLog so script-driven output and
// the editor's Console pane stay in sync.
class ScriptHost {
public:
    static constexpr size_t kOutputLogMax = 128;

    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&)            = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Receives an AnimationEvent firing. With Lua enabled the host
    // calls the global `OnAnimationEvent(entity, anim, event, s,
    // f, i, b, vx, vy, r, g, bl, a)` Lua function; without Lua it
    // falls back to the M6c1 stub formatter so the editor still
    // shows dispatches in the Console pane.
    void OnAnimationEvent(const std::string& entityName,
                          const std::string& animationName,
                          const AnimationEvent& event);

    // Read-only access to the rolling output buffer. Console panel
    // walks it in reverse to show newest-first the same way the
    // animation event log does.
    const std::vector<std::string>& OutputLog() const { return m_outputLog; }
    void ClearOutputLog() { m_outputLog.clear(); }

    // Public so the Lua-side print redirector (a static C function
    // wired into the lua_State) can push lines without needing
    // friend declarations. Trim-from-front keeps the buffer
    // bounded.
    void AppendLine(const std::string& line);

private:
    std::vector<std::string> m_outputLog;
    // Opaque lua_State* when NARAGAME_SCENE_EDITOR_HAS_LUA is
    // defined; nullptr otherwise. The header avoids including
    // <lua.h> so callers don't pay for it.
    void* m_lua = nullptr;
};

} // namespace SceneEditor
