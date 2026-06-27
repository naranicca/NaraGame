#include "ScriptComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void ScriptComponent::Serialize(Value& out) const {
    out.Add("script",        Value::MakeString(script.ToString()));
    out.Add("entryFunction", Value::MakeString(entryFunction));
}

void ScriptComponent::Deserialize(const Value& obj) {
    script        = AssetGuid::FromString(obj.GetString("script"));
    entryFunction = obj.GetString("entryFunction");
}

} // namespace SceneEditor
