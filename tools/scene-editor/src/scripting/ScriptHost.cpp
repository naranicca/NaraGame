#include "ScriptHost.h"

#include <cstdio>

namespace SceneEditor {

ScriptHost::ScriptHost()  = default;
ScriptHost::~ScriptHost() = default;

void ScriptHost::AppendLine(const std::string& line) {
    m_outputLog.push_back(line);
    if (m_outputLog.size() > kOutputLogMax) {
        m_outputLog.erase(
            m_outputLog.begin(),
            m_outputLog.begin() +
                (m_outputLog.size() - kOutputLogMax));
    }
}

void ScriptHost::OnAnimationEvent(
    const std::string& entityName,
    const std::string& animationName,
    const AnimationEvent& event) {
    // Build a "function call" shape -- mirrors what the eventual
    // Lua dispatch will read like ("OnFootstep(left, 0.0, 0)") so
    // the editor preview reads the same as the future scripted
    // call site.
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

} // namespace SceneEditor
