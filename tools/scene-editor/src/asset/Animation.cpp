#include "Animation.h"

#include <NaraGame/core/Log.h>

#include <cmath>
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

void AnimationAsset::FromJson(const Value& root) {
    texture = AssetGuid::FromString(root.GetString("texture"));
    loop    = root.GetBool("loop", true);
    frames.clear();
    const Value* arr = root.Find("frames");
    if (!arr || !arr->IsArray()) return;
    for (const Value& f : arr->arrayValue) {
        if (!f.IsObject()) continue;
        AnimationFrame fr;
        fr.sprite   = f.GetString("sprite");
        fr.duration = float(f.GetNumber("duration", 0.1));
        frames.push_back(std::move(fr));
    }
}

Value AnimationAsset::ToJson() const {
    Value root = Value::MakeObject();
    root.Add("schema",  Value::MakeNumber(1));
    root.Add("texture", Value::MakeString(texture.ToString()));
    root.Add("loop",    Value::MakeBool(loop));
    Value arr = Value::MakeArray();
    for (const AnimationFrame& fr : frames) {
        Value f = Value::MakeObject();
        f.Add("sprite",   Value::MakeString(fr.sprite));
        f.Add("duration", Value::MakeNumber(fr.duration));
        arr.Push(std::move(f));
    }
    root.Add("frames", std::move(arr));
    return root;
}

bool AnimationAsset::LoadFromFile(const std::string& path) {
    std::string text;
    if (!ReadFileText(path, text)) {
        NARAGAME_ERROR("Animation::Load: cannot read '%s'", path.c_str());
        return false;
    }
    std::string err;
    const Value root = NaraGame::MiniJson::Parse(text, &err);
    if (!root.IsObject()) {
        NARAGAME_ERROR("Animation::Load: '%s' parse failed: %s",
                       path.c_str(), err.c_str());
        return false;
    }
    FromJson(root);
    return true;
}

bool AnimationAsset::SaveToFile(const std::string& path) const {
    const std::string text = NaraGame::MiniJson::Write(ToJson());
    if (!WriteFileText(path, text)) {
        NARAGAME_ERROR("Animation::Save: cannot write '%s'", path.c_str());
        return false;
    }
    return true;
}

float AnimationAsset::TotalDuration() const {
    float total = 0.0f;
    for (const AnimationFrame& fr : frames) total += fr.duration;
    return total;
}

const AnimationFrame* AnimationAsset::Sample(float t) const {
    if (frames.empty()) return nullptr;
    const float total = TotalDuration();
    if (total <= 0.0f) return &frames.front();
    float u = loop ? std::fmod(t, total) : std::min(t, total);
    if (u < 0.0f) u = 0.0f;
    float acc = 0.0f;
    for (const AnimationFrame& fr : frames) {
        acc += fr.duration;
        if (u < acc) return &fr;
    }
    return &frames.back();
}

} // namespace SceneEditor
