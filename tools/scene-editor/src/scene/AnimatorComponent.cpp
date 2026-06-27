#include "AnimatorComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void AnimatorComponent::Serialize(Value& out) const {
    out.Add("animation",    Value::MakeString(animation.ToString()));
    out.Add("currentTime",  Value::MakeNumber(currentTime));
    out.Add("isPlaying",    Value::MakeBool(isPlaying));
    out.Add("controller",   Value::MakeString(controller.ToString()));
    out.Add("currentState", Value::MakeNumber(currentState));
    Value pars = Value::MakeObject();
    for (const auto& kv : paramValues) {
        pars.Add(kv.first, Value::MakeBool(kv.second));
    }
    out.Add("params", std::move(pars));
}

void AnimatorComponent::Deserialize(const Value& obj) {
    animation    = AssetGuid::FromString(obj.GetString("animation"));
    currentTime  = float(obj.GetNumber("currentTime", 0.0));
    isPlaying    = obj.GetBool("isPlaying", true);
    controller   = AssetGuid::FromString(obj.GetString("controller"));
    currentState = obj.GetInt("currentState", -1);
    paramValues.clear();
    const Value* pars = obj.Find("params");
    if (pars && pars->IsObject()) {
        for (size_t i = 0; i < pars->objectKeys.size(); ++i) {
            const Value& v = pars->objectValues[i];
            paramValues[pars->objectKeys[i]] =
                v.IsBool() ? v.boolValue : false;
        }
    }
}

} // namespace SceneEditor
