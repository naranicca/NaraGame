#include "BoxColliderComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void BoxColliderComponent::Serialize(Value& out) const {
    Value sz = Value::MakeArray();
    sz.Push(Value::MakeNumber(size.x));
    sz.Push(Value::MakeNumber(size.y));
    out.Add("size", std::move(sz));
    Value off = Value::MakeArray();
    off.Push(Value::MakeNumber(offset.x));
    off.Push(Value::MakeNumber(offset.y));
    out.Add("offset", std::move(off));
    out.Add("isTrigger", Value::MakeBool(isTrigger));
    out.Add("layer", Value::MakeString(layer));
}

void BoxColliderComponent::Deserialize(const Value& obj) {
    if (const Value* v = obj.Find("size")) {
        if (v->IsArray() && v->arrayValue.size() >= 2) {
            auto pick = [&](size_t i) {
                const Value& vv = v->arrayValue[i];
                return vv.IsNumber() ? float(vv.numberValue) : 1.0f;
            };
            size = { pick(0), pick(1) };
        }
    }
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
    layer     = obj.GetString("layer", "Default");
}

} // namespace SceneEditor
