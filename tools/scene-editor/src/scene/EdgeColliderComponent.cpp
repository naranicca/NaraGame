#include "EdgeColliderComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

EdgeColliderComponent::EdgeColliderComponent() {
    // Default: a horizontal 1-unit segment centred on the
    // entity's origin so a freshly-added Edge component is
    // visible + useful without editing.
    vertices.push_back({ -0.5f, 0.0f });
    vertices.push_back({  0.5f, 0.0f });
}

void EdgeColliderComponent::Serialize(Value& out) const {
    Value verts = Value::MakeArray();
    for (const auto& v : vertices) {
        Value vv = Value::MakeArray();
        vv.Push(Value::MakeNumber(v.x));
        vv.Push(Value::MakeNumber(v.y));
        verts.Push(std::move(vv));
    }
    out.Add("vertices", std::move(verts));
    out.Add("closed",    Value::MakeBool(closed));
    Value off = Value::MakeArray();
    off.Push(Value::MakeNumber(offset.x));
    off.Push(Value::MakeNumber(offset.y));
    out.Add("offset",    std::move(off));
    out.Add("isTrigger", Value::MakeBool(isTrigger));
    out.Add("layer",     Value::MakeString(layer));
}

void EdgeColliderComponent::Deserialize(const Value& obj) {
    vertices.clear();
    if (const Value* vs = obj.Find("vertices")) {
        if (vs->IsArray()) {
            vertices.reserve(vs->arrayValue.size());
            for (const Value& vv : vs->arrayValue) {
                if (!vv.IsArray() || vv.arrayValue.size() < 2) continue;
                auto pick = [&](size_t i) {
                    const Value& x = vv.arrayValue[i];
                    return x.IsNumber() ? float(x.numberValue) : 0.0f;
                };
                vertices.push_back({ pick(0), pick(1) });
            }
        }
    }
    closed = obj.GetBool("closed", false);
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
