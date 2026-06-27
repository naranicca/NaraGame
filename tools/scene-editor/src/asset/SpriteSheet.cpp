#include "SpriteSheet.h"

#include <cstdio>

namespace SceneEditor {

using NaraGame::MiniJson::Value;

namespace {

Value Vec2ToJson(const NaraGame::Vec2& v) {
    Value a = Value::MakeArray();
    a.Push(Value::MakeNumber(v.x));
    a.Push(Value::MakeNumber(v.y));
    return a;
}

NaraGame::Vec2 JsonToVec2(const Value* node, const NaraGame::Vec2& def) {
    if (!node || !node->IsArray() || node->arrayValue.size() < 2) return def;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? float(v.numberValue) : 0.0f;
    };
    return {pick(0), pick(1)};
}

Value RectToJson(const SubSprite& s) {
    Value a = Value::MakeArray();
    a.Push(Value::MakeNumber(s.x));
    a.Push(Value::MakeNumber(s.y));
    a.Push(Value::MakeNumber(s.w));
    a.Push(Value::MakeNumber(s.h));
    return a;
}

void RectFromJson(const Value* node, SubSprite& out) {
    if (!node || !node->IsArray() || node->arrayValue.size() < 4) return;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? int(v.numberValue) : 0;
    };
    out.x = pick(0);
    out.y = pick(1);
    out.w = pick(2);
    out.h = pick(3);
}

Value BorderToJson(const SubSprite& s) {
    Value a = Value::MakeArray();
    a.Push(Value::MakeNumber(s.borderLeft));
    a.Push(Value::MakeNumber(s.borderRight));
    a.Push(Value::MakeNumber(s.borderTop));
    a.Push(Value::MakeNumber(s.borderBottom));
    return a;
}

void BorderFromJson(const Value* node, SubSprite& out) {
    if (!node || !node->IsArray() || node->arrayValue.size() < 4) return;
    auto pick = [&](size_t i) {
        const Value& v = node->arrayValue[i];
        return v.IsNumber() ? int(v.numberValue) : 0;
    };
    out.borderLeft   = pick(0);
    out.borderRight  = pick(1);
    out.borderTop    = pick(2);
    out.borderBottom = pick(3);
}

} // namespace

void SpriteSheet::FromJson(const Value& obj) {
    sprites.clear();
    const Value* arr = obj.Find("sprites");
    if (!arr || !arr->IsArray()) return;
    sprites.reserve(arr->arrayValue.size());
    for (const Value& v : arr->arrayValue) {
        SubSprite s;
        s.name  = v.GetString("name");
        RectFromJson(v.Find("rect"), s);
        s.pivot = JsonToVec2(v.Find("pivot"), {0.5f, 0.5f});
        BorderFromJson(v.Find("border"), s);
        sprites.push_back(std::move(s));
    }
}

Value SpriteSheet::ToJson() const {
    Value root = Value::MakeObject();
    Value arr  = Value::MakeArray();
    for (const SubSprite& s : sprites) {
        Value o = Value::MakeObject();
        o.Add("name",   Value::MakeString(s.name));
        o.Add("rect",   RectToJson(s));
        o.Add("pivot",  Vec2ToJson(s.pivot));
        o.Add("border", BorderToJson(s));
        arr.Push(std::move(o));
    }
    root.Add("sprites", std::move(arr));
    return root;
}

namespace {

std::string MakeName(const std::string& prefix, int idx) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s_%d",
                  prefix.empty() ? "sprite" : prefix.c_str(), idx);
    return buf;
}

} // namespace

std::vector<SubSprite> SliceByCellSize(int texW, int texH, int cellW,
                                       int cellH,
                                       const std::string& namePrefix) {
    std::vector<SubSprite> out;
    if (texW <= 0 || texH <= 0 || cellW <= 0 || cellH <= 0) return out;
    int idx = 0;
    for (int y = 0; y + cellH <= texH; y += cellH) {
        for (int x = 0; x + cellW <= texW; x += cellW) {
            SubSprite s;
            s.name = MakeName(namePrefix, idx++);
            s.x = x; s.y = y; s.w = cellW; s.h = cellH;
            out.push_back(std::move(s));
        }
    }
    return out;
}

std::vector<SubSprite> SliceByCellCount(int texW, int texH, int cols,
                                        int rows,
                                        const std::string& namePrefix) {
    if (cols <= 0 || rows <= 0) return {};
    const int cellW = texW / cols;
    const int cellH = texH / rows;
    return SliceByCellSize(texW, texH, cellW, cellH, namePrefix);
}

} // namespace SceneEditor
