#include "AnimatorComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void AnimatorComponent::Serialize(Value& out) const {
    out.Add("animation",   Value::MakeString(animation.ToString()));
    out.Add("currentTime", Value::MakeNumber(currentTime));
    out.Add("isPlaying",   Value::MakeBool(isPlaying));
}

void AnimatorComponent::Deserialize(const Value& obj) {
    animation   = AssetGuid::FromString(obj.GetString("animation"));
    currentTime = float(obj.GetNumber("currentTime", 0.0));
    isPlaying   = obj.GetBool("isPlaying", true);
}

} // namespace SceneEditor
