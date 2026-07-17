#include "PolygonColliderComponent.h"

#include <cmath>

namespace SceneEditor {

using NaraGame::MiniJson::Value;

PolygonColliderComponent::PolygonColliderComponent() {
    // Regular pentagon, radius 0.4, first vertex at top
    // (theta = +pi/2) so the default visual matches a tradi-
    // tional "house" pentagon.
    const float r   = 0.4f;
    const int   n   = 5;
    const float pi  = 3.14159265358979323846f;
    vertices.reserve(n);
    for (int i = 0; i < n; ++i) {
        const float th = pi * 0.5f + float(i) *
                          (2.0f * pi / float(n));
        vertices.push_back({ r * std::cos(th), r * std::sin(th) });
    }
}

void PolygonColliderComponent::Serialize(Value& out) const {
    Value verts = Value::MakeArray();
    for (const auto& v : vertices) {
        Value vv = Value::MakeArray();
        vv.Push(Value::MakeNumber(v.x));
        vv.Push(Value::MakeNumber(v.y));
        verts.Push(std::move(vv));
    }
    out.Add("vertices", std::move(verts));
    Value off = Value::MakeArray();
    off.Push(Value::MakeNumber(offset.x));
    off.Push(Value::MakeNumber(offset.y));
    out.Add("offset",     std::move(off));
    out.Add("isTrigger",  Value::MakeBool(isTrigger));
    out.Add("layer",      Value::MakeString(layer));
}

void PolygonColliderComponent::Deserialize(const Value& obj) {
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
