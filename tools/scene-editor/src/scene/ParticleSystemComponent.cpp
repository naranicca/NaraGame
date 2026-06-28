#include "ParticleSystemComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

namespace {

Value ColorToJson(const NaraGame::Color& c) {
    Value a = Value::MakeArray();
    a.Push(Value::MakeNumber(c.r));
    a.Push(Value::MakeNumber(c.g));
    a.Push(Value::MakeNumber(c.b));
    a.Push(Value::MakeNumber(c.a));
    return a;
}

NaraGame::Color JsonToColor(const Value* node,
                             const NaraGame::Color& def) {
    if (!node || !node->IsArray() ||
        node->arrayValue.size() < 4) return def;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? float(v.numberValue) : 0.0f;
    };
    return { pick(0), pick(1), pick(2), pick(3) };
}

Value Vec2ToJson(const NaraGame::Vec2& v) {
    Value a = Value::MakeArray();
    a.Push(Value::MakeNumber(v.x));
    a.Push(Value::MakeNumber(v.y));
    return a;
}

NaraGame::Vec2 JsonToVec2(const Value* node,
                          const NaraGame::Vec2& def) {
    if (!node || !node->IsArray() ||
        node->arrayValue.size() < 2) return def;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? float(v.numberValue) : 0.0f;
    };
    return { pick(0), pick(1) };
}

} // namespace

void ParticleSystemComponent::Serialize(Value& out) const {
    out.Add("maxParticles",   Value::MakeNumber(maxParticles));
    out.Add("emissionRate",   Value::MakeNumber(emissionRate));
    out.Add("lifetimeMin",    Value::MakeNumber(lifetimeMin));
    out.Add("lifetimeMax",    Value::MakeNumber(lifetimeMax));
    out.Add("startSpeedMin",  Value::MakeNumber(startSpeedMin));
    out.Add("startSpeedMax",  Value::MakeNumber(startSpeedMax));
    out.Add("startSizeMin",   Value::MakeNumber(startSizeMin));
    out.Add("startSizeMax",   Value::MakeNumber(startSizeMax));
    out.Add("dirAngleMinDeg", Value::MakeNumber(dirAngleMinDeg));
    out.Add("dirAngleMaxDeg", Value::MakeNumber(dirAngleMaxDeg));
    out.Add("startColor",     ColorToJson(startColor));
    out.Add("endColor",       ColorToJson(endColor));
    out.Add("gravity",        Vec2ToJson(gravity));
    out.Add("texture",        Value::MakeString(texture.ToString()));
    out.Add("playOnStart",    Value::MakeBool(playOnStart));
    out.Add("loop",           Value::MakeBool(loop));
}

void ParticleSystemComponent::Deserialize(const Value& obj) {
    maxParticles   = obj.GetInt   ("maxParticles",   200);
    emissionRate   = float(obj.GetNumber("emissionRate",   30.0));
    lifetimeMin    = float(obj.GetNumber("lifetimeMin",     0.8));
    lifetimeMax    = float(obj.GetNumber("lifetimeMax",     1.4));
    startSpeedMin  = float(obj.GetNumber("startSpeedMin",   1.0));
    startSpeedMax  = float(obj.GetNumber("startSpeedMax",   2.5));
    startSizeMin   = float(obj.GetNumber("startSizeMin",    0.08));
    startSizeMax   = float(obj.GetNumber("startSizeMax",    0.18));
    dirAngleMinDeg = float(obj.GetNumber("dirAngleMinDeg", 45.0));
    dirAngleMaxDeg = float(obj.GetNumber("dirAngleMaxDeg", 135.0));
    startColor     = JsonToColor(obj.Find("startColor"),
                                  NaraGame::Color::White());
    endColor       = JsonToColor(obj.Find("endColor"),
                                  NaraGame::Color{1.0f, 1.0f, 1.0f, 0.0f});
    gravity        = JsonToVec2(obj.Find("gravity"),
                                 NaraGame::Vec2{0.0f, -2.0f});
    texture        = AssetGuid::FromString(obj.GetString("texture", ""));
    playOnStart    = obj.GetBool("playOnStart", true);
    loop           = obj.GetBool("loop",         true);
    // Reset runtime state so a scene reload starts the
    // emitter cleanly. The pool itself is sized on first
    // Tick to avoid forcing every deserialize to commit
    // to the current maxParticles value.
    _pool.clear();
    _liveCount    = 0;
    _emitAccum    = 0.0f;
    _emittedTotal = 0;
    _started      = false;
    _rng          = 0;
}

} // namespace SceneEditor
