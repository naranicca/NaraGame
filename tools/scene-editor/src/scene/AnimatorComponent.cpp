#include "AnimatorComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void AnimatorComponent::Serialize(Value& out) const {
    out.Add("animation",    Value::MakeString(animation.ToString()));
    out.Add("currentTime",  Value::MakeNumber(currentTime));
    out.Add("isPlaying",    Value::MakeBool(isPlaying));
    out.Add("controller",   Value::MakeString(controller.ToString()));
    out.Add("currentState", Value::MakeNumber(currentState));
    Value pb = Value::MakeObject();
    for (const auto& kv : paramBools)  pb.Add(kv.first, Value::MakeBool(kv.second));
    out.Add("paramBools", std::move(pb));
    Value pf = Value::MakeObject();
    for (const auto& kv : paramFloats) pf.Add(kv.first, Value::MakeNumber(kv.second));
    out.Add("paramFloats", std::move(pf));
    Value pi = Value::MakeObject();
    for (const auto& kv : paramInts)   pi.Add(kv.first, Value::MakeNumber(kv.second));
    out.Add("paramInts", std::move(pi));
}

void AnimatorComponent::Deserialize(const Value& obj) {
    animation    = AssetGuid::FromString(obj.GetString("animation"));
    currentTime  = float(obj.GetNumber("currentTime", 0.0));
    isPlaying    = obj.GetBool("isPlaying", true);
    controller   = AssetGuid::FromString(obj.GetString("controller"));
    currentState = obj.GetInt("currentState", -1);
    paramBools.clear();
    paramFloats.clear();
    paramInts.clear();
    // Backward-compat: M5c4-c8 wrote a single "params" bool map. Read
    // it into paramBools when present so old scenes load unchanged.
    if (const Value* legacy = obj.Find("params")) {
        if (legacy->IsObject()) {
            for (size_t i = 0; i < legacy->objectKeys.size(); ++i) {
                const Value& v = legacy->objectValues[i];
                paramBools[legacy->objectKeys[i]] =
                    v.IsBool() ? v.boolValue : false;
            }
        }
    }
    if (const Value* pb = obj.Find("paramBools")) {
        if (pb->IsObject()) {
            for (size_t i = 0; i < pb->objectKeys.size(); ++i) {
                const Value& v = pb->objectValues[i];
                paramBools[pb->objectKeys[i]] =
                    v.IsBool() ? v.boolValue : false;
            }
        }
    }
    if (const Value* pf = obj.Find("paramFloats")) {
        if (pf->IsObject()) {
            for (size_t i = 0; i < pf->objectKeys.size(); ++i) {
                const Value& v = pf->objectValues[i];
                paramFloats[pf->objectKeys[i]] =
                    v.IsNumber() ? float(v.numberValue) : 0.0f;
            }
        }
    }
    if (const Value* pi = obj.Find("paramInts")) {
        if (pi->IsObject()) {
            for (size_t i = 0; i < pi->objectKeys.size(); ++i) {
                const Value& v = pi->objectValues[i];
                paramInts[pi->objectKeys[i]] =
                    v.IsNumber() ? int(v.numberValue) : 0;
            }
        }
    }
}

} // namespace SceneEditor
