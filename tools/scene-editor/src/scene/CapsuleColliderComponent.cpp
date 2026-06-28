#include "CapsuleColliderComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void CapsuleColliderComponent::Serialize(Value& out) const {
    out.Add("radius",     Value::MakeNumber(radius));
    out.Add("halfLength", Value::MakeNumber(halfLength));
    Value off = Value::MakeArray();
    off.Push(Value::MakeNumber(offset.x));
    off.Push(Value::MakeNumber(offset.y));
    out.Add("offset", std::move(off));
    out.Add("isTrigger", Value::MakeBool(isTrigger));
    out.Add("layer",     Value::MakeString(layer));
}

void CapsuleColliderComponent::Deserialize(const Value& obj) {
    radius     = float(obj.GetNumber("radius",     0.25));
    halfLength = float(obj.GetNumber("halfLength", 0.25));
    if (const Value* v = obj.Find("offset")) {
        if (v->IsArray() && v->arrayValue.size() >= 2) {
            auto pick = [&](size_t i) {
                const Value& vv = v->arrayValue[i];
                return vv.IsNumber() ? float(vv.numberValue) : 0.0f;
            };
            offset = { pick(0), pick(1) };
        }
    }
    isTrigger = obj.GetBool("isTrigger", false);
    layer     = obj.GetString("layer",   "Default");
}

} // namespace SceneEditor
