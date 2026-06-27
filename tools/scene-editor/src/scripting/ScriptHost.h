#pragma once

#include "../asset/Animation.h"

#include <string>
#include <vector>

namespace SceneEditor {

// Editor-side scripting host. M6c1 ships the dispatch surface +
// rolling output log so the runtime + UI plumbing lands first;
// the actual Lua state (sol2 + libluacore) plugs in here in M6c2.
//
// The host's job is to receive engine-side events (animation hooks,
// component lifecycle calls, runtime parameter mutations) and route
// them through whichever script the matching ScriptComponent has
// attached. While the underlying interpreter is still a stub each
// dispatched event just lands in m_outputLog so the user can verify
// the wiring through the Console panel.
class ScriptHost {
public:
    static constexpr size_t kOutputLogMax = 128;

    ScriptHost();
    ~ScriptHost();

    // Receives an AnimationEvent firing. Until the Lua bridge is
    // in place this only formats + appends a line to the output
    // log; the call signature already matches what the eventual
    // host->script dispatch will need.
    void OnAnimationEvent(const std::string& entityName,
                          const std::string& animationName,
                          const AnimationEvent& event);

    // Read-only access to the rolling output buffer. Console panel
    // walks it in reverse to show newest-first the same way the
    // animation event log does.
    const std::vector<std::string>& OutputLog() const { return m_outputLog; }
    void ClearOutputLog() { m_outputLog.clear(); }

private:
    // Trim from the front so a runaway emitter can't grow the
    // buffer without bound.
    void AppendLine(const std::string& line);

    std::vector<std::string> m_outputLog;
};

} // namespace SceneEditor
