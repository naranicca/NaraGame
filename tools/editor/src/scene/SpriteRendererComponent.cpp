#include "SpriteRendererComponent.h"

namespace SceneEditor {

namespace {

using NaraGame::MiniJson::Value;

const char* DrawModeName(SpriteDrawMode m) {
    switch (m) {
        case SpriteDrawMode::Simple: return "Simple";
        case SpriteDrawMode::Sliced: return "Sliced";
        case SpriteDrawMode::Tiled:  return "Tiled";
    }
    return "Simple";
}

SpriteDrawMode ParseDrawMode(const std::string& s, SpriteDrawMode def) {
    if (s == "Simple") return SpriteDrawMode::Simple;
    if (s == "Sliced") return SpriteDrawMode::Sliced;
    if (s == "Tiled")  return SpriteDrawMode::Tiled;
    return def;
}

Value ColorToJson(const NaraGame::Color& c) {
    Value arr = Value::MakeArray();
    arr.Push(Value::MakeNumber(c.r));
    arr.Push(Value::MakeNumber(c.g));
    arr.Push(Value::MakeNumber(c.b));
    arr.Push(Value::MakeNumber(c.a));
    return arr;
}

NaraGame::Color JsonToColor(const Value* node, const NaraGame::Color& def) {
    if (!node || !node->IsArray() || node->arrayValue.size() < 4) return def;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? float(v.numberValue) : 0.0f;
    };
    return {pick(0), pick(1), pick(2), pick(3)};
}

Value Vec2ToJson(const NaraGame::Vec2& v) {
    Value arr = Value::MakeArray();
    arr.Push(Value::MakeNumber(v.x));
    arr.Push(Value::MakeNumber(v.y));
    return arr;
}

NaraGame::Vec2 JsonToVec2(const Value* node, const NaraGame::Vec2& def) {
    if (!node || !node->IsArray() || node->arrayValue.size() < 2) return def;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? float(v.numberValue) : 0.0f;
    };
    return {pick(0), pick(1)};
}

Value Vec4ToJson(const NaraGame::Vec4& v) {
    Value arr = Value::MakeArray();
    arr.Push(Value::MakeNumber(v.x)); arr.Push(Value::MakeNumber(v.y));
    arr.Push(Value::MakeNumber(v.z)); arr.Push(Value::MakeNumber(v.w));
    return arr;
}

NaraGame::Vec4 JsonToVec4(const Value* node, const NaraGame::Vec4& def) {
    if (!node || !node->IsArray() || node->arrayValue.size() < 4) return def;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? float(v.numberValue) : 0.0f;
    };
    return {pick(0), pick(1), pick(2), pick(3)};
}

} // namespace

void SpriteRendererComponent::Serialize(Value& out) const {
    out.Add("sprite",       Value::MakeString(sprite.ToString()));
    out.Add("subSprite",    Value::MakeString(subSprite));
    out.Add("color",        ColorToJson(color));
    out.Add("flipX",        Value::MakeBool(flipX));
    out.Add("flipY",        Value::MakeBool(flipY));
    out.Add("drawMode",     Value::MakeString(DrawModeName(drawMode)));
    out.Add("size",         Vec2ToJson(size));
    out.Add("customQuad",   Value::MakeBool(customQuad));
    out.Add("quadTL",       Vec2ToJson(quadTL));
    out.Add("quadTR",       Vec2ToJson(quadTR));
    out.Add("quadBR",       Vec2ToJson(quadBR));
    out.Add("quadBL",       Vec2ToJson(quadBL));
    out.Add("uvRect",       Vec4ToJson(uvRect));
    out.Add("sortingLayer", Value::MakeString(sortingLayer));
    out.Add("orderInLayer", Value::MakeNumber(orderInLayer));
}

void SpriteRendererComponent::Deserialize(const Value& obj) {
    sprite       = AssetGuid::FromString(obj.GetString("sprite"));
    subSprite    = obj.GetString("subSprite");
    color        = JsonToColor(obj.Find("color"), NaraGame::Color::White());
    flipX        = obj.GetBool("flipX", false);
    flipY        = obj.GetBool("flipY", false);
    drawMode     = ParseDrawMode(obj.GetString("drawMode"),
                                 SpriteDrawMode::Simple);
    size         = JsonToVec2(obj.Find("size"), {1.0f, 1.0f});
    customQuad   = obj.GetBool("customQuad", false);
    quadTL       = JsonToVec2(obj.Find("quadTL"), {-0.5f,  0.5f});
    quadTR       = JsonToVec2(obj.Find("quadTR"), { 0.5f,  0.5f});
    quadBR       = JsonToVec2(obj.Find("quadBR"), { 0.5f, -0.5f});
    quadBL       = JsonToVec2(obj.Find("quadBL"), {-0.5f, -0.5f});
    uvRect       = JsonToVec4(obj.Find("uvRect"), {0.0f, 0.0f, 1.0f, 1.0f});
    sortingLayer = obj.GetString("sortingLayer", "Default");
    orderInLayer = obj.GetInt("orderInLayer", 0);
}

} // namespace SceneEditor
