#include "AnimatorController.h"

#include <NaraGame/core/Log.h>

#include <fstream>
#include <sstream>

namespace SceneEditor {

using NaraGame::MiniJson::Value;

namespace {

bool ReadFileText(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool WriteFileText(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << content;
    return out.good();
}

} // namespace

void AnimatorControllerAsset::FromJson(const Value& root) {
    states.clear();
    parameters.clear();
    transitions.clear();
    defaultState = root.GetInt("defaultState", 0);

    if (const Value* arr = root.Find("states")) {
        if (arr->IsArray()) {
            for (const Value& s : arr->arrayValue) {
                if (!s.IsObject()) continue;
                AnimatorState st;
                st.name      = s.GetString("name");
                st.animation = AssetGuid::FromString(s.GetString("animation"));
                st.speed     = float(s.GetNumber("speed", 1.0));
                states.push_back(std::move(st));
            }
        }
    }
    if (const Value* arr = root.Find("parameters")) {
        if (arr->IsArray()) {
            for (const Value& p : arr->arrayValue) {
                if (!p.IsObject()) continue;
                AnimatorParameter par;
                par.name         = p.GetString("name");
                par.defaultValue = p.GetBool("defaultValue", false);
                parameters.push_back(std::move(par));
            }
        }
    }
    if (const Value* arr = root.Find("transitions")) {
        if (arr->IsArray()) {
            for (const Value& t : arr->arrayValue) {
                if (!t.IsObject()) continue;
                AnimatorTransition tr;
                tr.fromIdx  = t.GetInt("fromIdx", -1);
                tr.toIdx    = t.GetInt("toIdx", 0);
                tr.param    = t.GetString("param");
                tr.expected = t.GetBool("expected", true);
                transitions.push_back(std::move(tr));
            }
        }
    }
}

Value AnimatorControllerAsset::ToJson() const {
    Value root = Value::MakeObject();
    root.Add("schema",       Value::MakeNumber(1));
    root.Add("defaultState", Value::MakeNumber(defaultState));
    Value sarr = Value::MakeArray();
    for (const AnimatorState& s : states) {
        Value o = Value::MakeObject();
        o.Add("name",      Value::MakeString(s.name));
        o.Add("animation", Value::MakeString(s.animation.ToString()));
        o.Add("speed",     Value::MakeNumber(s.speed));
        sarr.Push(std::move(o));
    }
    root.Add("states", std::move(sarr));
    Value parr = Value::MakeArray();
    for (const AnimatorParameter& p : parameters) {
        Value o = Value::MakeObject();
        o.Add("name",         Value::MakeString(p.name));
        o.Add("defaultValue", Value::MakeBool(p.defaultValue));
        parr.Push(std::move(o));
    }
    root.Add("parameters", std::move(parr));
    Value tarr = Value::MakeArray();
    for (const AnimatorTransition& t : transitions) {
        Value o = Value::MakeObject();
        o.Add("fromIdx",  Value::MakeNumber(t.fromIdx));
        o.Add("toIdx",    Value::MakeNumber(t.toIdx));
        o.Add("param",    Value::MakeString(t.param));
        o.Add("expected", Value::MakeBool(t.expected));
        tarr.Push(std::move(o));
    }
    root.Add("transitions", std::move(tarr));
    return root;
}

bool AnimatorControllerAsset::LoadFromFile(const std::string& path) {
    std::string text;
    if (!ReadFileText(path, text)) {
        NARAGAME_ERROR("AnimatorController::Load: cannot read '%s'",
                       path.c_str());
        return false;
    }
    std::string err;
    const Value root = NaraGame::MiniJson::Parse(text, &err);
    if (!root.IsObject()) {
        NARAGAME_ERROR("AnimatorController::Load: '%s' parse failed: %s",
                       path.c_str(), err.c_str());
        return false;
    }
    FromJson(root);
    return true;
}

bool AnimatorControllerAsset::SaveToFile(const std::string& path) const {
    const std::string text = NaraGame::MiniJson::Write(ToJson());
    if (!WriteFileText(path, text)) {
        NARAGAME_ERROR("AnimatorController::Save: cannot write '%s'",
                       path.c_str());
        return false;
    }
    return true;
}

} // namespace SceneEditor
