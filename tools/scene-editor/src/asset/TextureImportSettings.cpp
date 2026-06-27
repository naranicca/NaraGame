#include "TextureImportSettings.h"

#include <cstring>

namespace SceneEditor {

using NaraGame::MiniJson::Value;
using NaraGame::Vec2;

const char* ToString(TextureImportSettings::PivotMode m) {
    switch (m) {
        case TextureImportSettings::PivotMode::Center:      return "Center";
        case TextureImportSettings::PivotMode::TopLeft:     return "TopLeft";
        case TextureImportSettings::PivotMode::TopRight:    return "TopRight";
        case TextureImportSettings::PivotMode::BottomLeft:  return "BottomLeft";
        case TextureImportSettings::PivotMode::BottomRight: return "BottomRight";
        case TextureImportSettings::PivotMode::Custom:      return "Custom";
    }
    return "Center";
}
const char* ToString(TextureImportSettings::Filter f) {
    return f == TextureImportSettings::Filter::Point ? "Point" : "Linear";
}
const char* ToString(TextureImportSettings::Wrap w) {
    switch (w) {
        case TextureImportSettings::Wrap::Clamp:  return "Clamp";
        case TextureImportSettings::Wrap::Repeat: return "Repeat";
        case TextureImportSettings::Wrap::Mirror: return "Mirror";
    }
    return "Clamp";
}

const char* ToString(TextureImportSettings::SpriteMode m) {
    switch (m) {
        case TextureImportSettings::SpriteMode::Single:   return "Single";
        case TextureImportSettings::SpriteMode::Multiple: return "Multiple";
    }
    return "Single";
}

namespace {

TextureImportSettings::PivotMode ParsePivot(const std::string& s,
                                            TextureImportSettings::PivotMode def) {
    if (s == "Center")      return TextureImportSettings::PivotMode::Center;
    if (s == "TopLeft")     return TextureImportSettings::PivotMode::TopLeft;
    if (s == "TopRight")    return TextureImportSettings::PivotMode::TopRight;
    if (s == "BottomLeft")  return TextureImportSettings::PivotMode::BottomLeft;
    if (s == "BottomRight") return TextureImportSettings::PivotMode::BottomRight;
    if (s == "Custom")      return TextureImportSettings::PivotMode::Custom;
    return def;
}
TextureImportSettings::Filter ParseFilter(const std::string& s,
                                          TextureImportSettings::Filter def) {
    if (s == "Linear") return TextureImportSettings::Filter::Linear;
    if (s == "Point")  return TextureImportSettings::Filter::Point;
    return def;
}
TextureImportSettings::Wrap ParseWrap(const std::string& s,
                                      TextureImportSettings::Wrap def) {
    if (s == "Clamp")  return TextureImportSettings::Wrap::Clamp;
    if (s == "Repeat") return TextureImportSettings::Wrap::Repeat;
    if (s == "Mirror") return TextureImportSettings::Wrap::Mirror;
    return def;
}

TextureImportSettings::SpriteMode ParseSpriteMode(
    const std::string& s, TextureImportSettings::SpriteMode def) {
    if (s == "Single")   return TextureImportSettings::SpriteMode::Single;
    if (s == "Multiple") return TextureImportSettings::SpriteMode::Multiple;
    return def;
}

Vec2 JsonToVec2(const Value* node, const Vec2& def) {
    if (!node || !node->IsArray() || node->arrayValue.size() < 2) return def;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? float(v.numberValue) : 0.0f;
    };
    return {pick(0), pick(1)};
}

Value Vec2ToJson(const Vec2& v) {
    Value arr = Value::MakeArray();
    arr.Push(Value::MakeNumber(v.x));
    arr.Push(Value::MakeNumber(v.y));
    return arr;
}

} // namespace

Vec2 TextureImportSettings::ResolvedPivot() const {
    switch (pivotMode) {
        case PivotMode::Center:      return {0.5f, 0.5f};
        case PivotMode::TopLeft:     return {0.0f, 1.0f};
        case PivotMode::TopRight:    return {1.0f, 1.0f};
        case PivotMode::BottomLeft:  return {0.0f, 0.0f};
        case PivotMode::BottomRight: return {1.0f, 0.0f};
        case PivotMode::Custom:      return customPivot;
    }
    return {0.5f, 0.5f};
}

void TextureImportSettings::FromJson(const Value& obj) {
    pixelsPerUnit       = float(obj.GetNumber("pixelsPerUnit", 100.0));
    pivotMode           = ParsePivot(obj.GetString("pivotMode"), PivotMode::Center);
    customPivot         = JsonToVec2(obj.Find("customPivot"), {0.5f, 0.5f});
    filterMode          = ParseFilter(obj.GetString("filterMode"), Filter::Linear);
    wrapMode            = ParseWrap(obj.GetString("wrapMode"), Wrap::Clamp);
    alphaIsTransparency = obj.GetBool("alphaIsTransparency", true);
    spriteMode          = ParseSpriteMode(obj.GetString("spriteMode"),
                                          SpriteMode::Single);
    spriteSheet = SpriteSheet{};
    if (const Value* sheet = obj.Find("spriteSheet")) {
        if (sheet->IsObject()) spriteSheet.FromJson(*sheet);
    }
}

Value TextureImportSettings::ToJson() const {
    Value o = Value::MakeObject();
    o.Add("pixelsPerUnit",       Value::MakeNumber(pixelsPerUnit));
    o.Add("pivotMode",           Value::MakeString(ToString(pivotMode)));
    o.Add("customPivot",         Vec2ToJson(customPivot));
    o.Add("filterMode",          Value::MakeString(ToString(filterMode)));
    o.Add("wrapMode",            Value::MakeString(ToString(wrapMode)));
    o.Add("alphaIsTransparency", Value::MakeBool(alphaIsTransparency));
    o.Add("spriteMode",          Value::MakeString(ToString(spriteMode)));
    if (spriteMode == SpriteMode::Multiple || !spriteSheet.sprites.empty()) {
        o.Add("spriteSheet", spriteSheet.ToJson());
    }
    return o;
}

} // namespace SceneEditor
