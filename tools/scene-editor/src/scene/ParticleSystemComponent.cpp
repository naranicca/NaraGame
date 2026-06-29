#include "ParticleSystemComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

namespace {

const char* ShapeName(ParticleEmissionShape s) {
    switch (s) {
    case ParticleEmissionShape::Point:        return "Point";
    case ParticleEmissionShape::CircleEdge:   return "CircleEdge";
    case ParticleEmissionShape::CircleVolume: return "CircleVolume";
    case ParticleEmissionShape::BoxEdge:      return "BoxEdge";
    case ParticleEmissionShape::BoxVolume:    return "BoxVolume";
    case ParticleEmissionShape::Line:         return "Line";
    }
    return "Point";
}

ParticleEmissionShape ParseShape(const std::string& s) {
    if (s == "CircleEdge")   return ParticleEmissionShape::CircleEdge;
    if (s == "CircleVolume") return ParticleEmissionShape::CircleVolume;
    if (s == "BoxEdge")      return ParticleEmissionShape::BoxEdge;
    if (s == "BoxVolume")    return ParticleEmissionShape::BoxVolume;
    if (s == "Line")         return ParticleEmissionShape::Line;
    return ParticleEmissionShape::Point;
}

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
    out.Add("endSizeMin",     Value::MakeNumber(endSizeMin));
    out.Add("endSizeMax",     Value::MakeNumber(endSizeMax));
    out.Add("dirAngleMinDeg", Value::MakeNumber(dirAngleMinDeg));
    out.Add("dirAngleMaxDeg", Value::MakeNumber(dirAngleMaxDeg));
    out.Add("startRotationMinDeg", Value::MakeNumber(startRotationMinDeg));
    out.Add("startRotationMaxDeg", Value::MakeNumber(startRotationMaxDeg));
    out.Add("angularSpeedMinDeg",  Value::MakeNumber(angularSpeedMinDeg));
    out.Add("angularSpeedMaxDeg",  Value::MakeNumber(angularSpeedMaxDeg));
    out.Add("velocityAlign",       Value::MakeBool(velocityAlign));
    out.Add("startColor",     ColorToJson(startColor));
    out.Add("endColor",       ColorToJson(endColor));
    out.Add("gravity",        Vec2ToJson(gravity));
    out.Add("shape",          Value::MakeString(ShapeName(shape)));
    out.Add("shapeRadius",    Value::MakeNumber(shapeRadius));
    out.Add("shapeBoxSize",   Vec2ToJson(shapeBoxSize));
    out.Add("shapeLineEnd",   Vec2ToJson(shapeLineEnd));
    out.Add("texture",        Value::MakeString(texture.ToString()));
    out.Add("subSprite",      Value::MakeString(subSprite));
    Value frames = Value::MakeArray();
    for (const std::string& f : animFrames) {
        frames.Push(Value::MakeString(f));
    }
    out.Add("animFrames",     std::move(frames));
    out.Add("animFps",        Value::MakeNumber(animFps));
    out.Add("animLoop",       Value::MakeBool(animLoop));
    out.Add("playOnStart",    Value::MakeBool(playOnStart));
    out.Add("loop",           Value::MakeBool(loop));
    out.Add("duration",       Value::MakeNumber(duration));
    Value burstArr = Value::MakeArray();
    for (const Burst& b : bursts) {
        Value entry = Value::MakeObject();
        entry.Add("time",  Value::MakeNumber(b.time));
        entry.Add("count", Value::MakeNumber(b.count));
        burstArr.Push(std::move(entry));
    }
    out.Add("bursts",         std::move(burstArr));
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
    // M9c8 legacy compat: missing endSize fields fall back to
    // the sentinel (-1), which the emit path then treats as
    // "use startSize" -- legacy scenes load with size that
    // stays constant, exactly as they did pre-M9c8.
    endSizeMin     = float(obj.GetNumber("endSizeMin", -1.0));
    endSizeMax     = float(obj.GetNumber("endSizeMax", -1.0));
    dirAngleMinDeg = float(obj.GetNumber("dirAngleMinDeg", 45.0));
    dirAngleMaxDeg = float(obj.GetNumber("dirAngleMaxDeg", 135.0));
    startRotationMinDeg = float(obj.GetNumber("startRotationMinDeg", 0.0));
    startRotationMaxDeg = float(obj.GetNumber("startRotationMaxDeg", 0.0));
    angularSpeedMinDeg  = float(obj.GetNumber("angularSpeedMinDeg",  0.0));
    angularSpeedMaxDeg  = float(obj.GetNumber("angularSpeedMaxDeg",  0.0));
    velocityAlign       = obj.GetBool("velocityAlign", false);
    startColor     = JsonToColor(obj.Find("startColor"),
                                  NaraGame::Color::White());
    endColor       = JsonToColor(obj.Find("endColor"),
                                  NaraGame::Color{1.0f, 1.0f, 1.0f, 0.0f});
    gravity        = JsonToVec2(obj.Find("gravity"),
                                 NaraGame::Vec2{0.0f, -2.0f});
    shape          = ParseShape(obj.GetString("shape", "Point"));
    shapeRadius    = float(obj.GetNumber("shapeRadius", 1.0));
    shapeBoxSize   = JsonToVec2(obj.Find("shapeBoxSize"),
                                 NaraGame::Vec2{1.0f, 1.0f});
    shapeLineEnd   = JsonToVec2(obj.Find("shapeLineEnd"),
                                 NaraGame::Vec2{1.0f, 0.0f});
    texture        = AssetGuid::FromString(obj.GetString("texture", ""));
    subSprite      = obj.GetString("subSprite", "");
    animFrames.clear();
    if (const Value* af = obj.Find("animFrames");
        af && af->IsArray()) {
        for (const Value& v : af->arrayValue) {
            if (v.IsString()) animFrames.push_back(v.stringValue);
        }
    }
    animFps        = float(obj.GetNumber("animFps", 8.0));
    animLoop       = obj.GetBool("animLoop", true);
    playOnStart    = obj.GetBool("playOnStart", true);
    loop           = obj.GetBool("loop",         true);
    duration       = float(obj.GetNumber("duration", 3.0));
    bursts.clear();
    if (const Value* arr = obj.Find("bursts");
        arr && arr->IsArray()) {
        for (const Value& b : arr->arrayValue) {
            if (!b.IsObject()) continue;
            Burst entry;
            entry.time  = float(b.GetNumber("time",  0.0));
            entry.count = b.GetInt("count", 10);
            bursts.push_back(entry);
        }
    }
    // Reset runtime state so a scene reload starts the
    // emitter cleanly. The pool itself is sized on first
    // Tick to avoid forcing every deserialize to commit
    // to the current maxParticles value.
    _pool.clear();
    _liveCount       = 0;
    _emitAccum       = 0.0f;
    _emittedTotal    = 0;
    _started         = false;
    _rng             = 0;
    _elapsed         = 0.0f;
    _burstNextIndex  = 0;
}

} // namespace SceneEditor
