#include "Rigidbody2DComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void Rigidbody2DComponent::Serialize(Value& out) const {
    Value vel = Value::MakeArray();
    vel.Push(Value::MakeNumber(velocity.x));
    vel.Push(Value::MakeNumber(velocity.y));
    out.Add("velocity", std::move(vel));
    Value grav = Value::MakeArray();
    grav.Push(Value::MakeNumber(gravity.x));
    grav.Push(Value::MakeNumber(gravity.y));
    out.Add("gravity", std::move(grav));
    out.Add("drag", Value::MakeNumber(drag));
    out.Add("mass", Value::MakeNumber(mass));
    out.Add("restitution", Value::MakeNumber(restitution));
    out.Add("friction", Value::MakeNumber(friction));
}

void Rigidbody2DComponent::Deserialize(const Value& obj) {
    auto readVec2 = [&](const char* key, NaraGame::Vec2& dst) {
        if (const Value* v = obj.Find(key)) {
            if (v->IsArray() && v->arrayValue.size() >= 2) {
                auto pick = [&](size_t i) {
                    const Value& vv = v->arrayValue[i];
                    return vv.IsNumber() ? float(vv.numberValue) : 0.0f;
                };
                dst = { pick(0), pick(1) };
            }
        }
    };
    readVec2("velocity", velocity);
    readVec2("gravity",  gravity);
    drag        = float(obj.GetNumber("drag", 0.0));
    mass        = float(obj.GetNumber("mass", 1.0));
    if (mass <= 0.0f) mass = 1.0f;
    restitution = float(obj.GetNumber("restitution", 0.0));
    friction    = float(obj.GetNumber("friction", 0.0));
}

} // namespace SceneEditor
