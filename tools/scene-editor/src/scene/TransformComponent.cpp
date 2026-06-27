#include "TransformComponent.h"

namespace SceneEditor {

namespace {

using NaraGame::MiniJson::Value;

Value Vec3ToJson(const NaraGame::Vec3& v) {
    Value arr = Value::MakeArray();
    arr.Push(Value::MakeNumber(v.x));
    arr.Push(Value::MakeNumber(v.y));
    arr.Push(Value::MakeNumber(v.z));
    return arr;
}

NaraGame::Vec3 JsonToVec3(const Value* node, const NaraGame::Vec3& def) {
    if (!node || !node->IsArray() || node->arrayValue.size() < 3) return def;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? float(v.numberValue) : 0.0f;
    };
    return {pick(0), pick(1), pick(2)};
}

} // namespace

void TransformComponent::Serialize(Value& out) const {
    out.Add("position", Vec3ToJson(position));
    out.Add("rotation", Value::MakeNumber(rotation));
    out.Add("scale",    Vec3ToJson(scale));
}

void TransformComponent::Deserialize(const Value& obj) {
    position = JsonToVec3(obj.Find("position"), {0.0f, 0.0f, 0.0f});
    rotation = float(obj.GetNumber("rotation", 0.0));
    scale    = JsonToVec3(obj.Find("scale"),    {1.0f, 1.0f, 1.0f});
}

} // namespace SceneEditor
