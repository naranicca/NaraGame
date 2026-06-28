#include "CircleColliderComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void CircleColliderComponent::Serialize(Value& out) const {
    out.Add("radius", Value::MakeNumber(radius));
    Value off = Value::MakeArray();
    off.Push(Value::MakeNumber(offset.x));
    off.Push(Value::MakeNumber(offset.y));
    out.Add("offset", std::move(off));
    out.Add("isTrigger", Value::MakeBool(isTrigger));
    out.Add("layer", Value::MakeString(layer));
}

void CircleColliderComponent::Deserialize(const Value& obj) {
    radius = float(obj.GetNumber("radius", 0.5));
    if (const Value* v = obj.Find("offset")) {
        if (v->IsArray() && v->arrayValue.size() >= 2) {
            auto pick = [&](size_t i) {
                const Value& vv = v->arrayValue[i];
                return vv.IsNumber() ? float(vv.numberValue) : 0.0f;
            };
            offset = { pick(0), pick(1) };
        }
    }
    isTrigger = obj.GetBool("isTrigger", true);
    layer     = obj.GetString("layer", "Default");
}

} // namespace SceneEditor
