#include "Autotile.h"

#include <NaraGame/core/Log.h>

#include <fstream>
#include <sstream>

namespace SceneEditor {

using NaraGame::MiniJson::Value;

namespace {

bool ReadFileText(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool WriteFileText(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << content;
    return out.good();
}

} // namespace

void AutotileAsset::RebuildTileSet() {
    tileSet.clear();
    for (int t : mask) tileSet.insert(t);
}

void AutotileAsset::FromJson(const Value& root) {
    texture    = AssetGuid::FromString(root.GetString("texture"));
    cellPixelW = root.GetInt("cellPixelW", 16);
    cellPixelH = root.GetInt("cellPixelH", 16);
    mask.fill(0);
    const Value* arr = root.Find("mask");
    if (arr && arr->IsArray()) {
        const size_t n = std::min<size_t>(16, arr->arrayValue.size());
        for (size_t i = 0; i < n; ++i) {
            const Value& v = arr->arrayValue[i];
            mask[i] = v.IsNumber() ? int(v.numberValue) : 0;
        }
    }
    RebuildTileSet();
}

Value AutotileAsset::ToJson() const {
    Value root = Value::MakeObject();
    root.Add("schema",     Value::MakeNumber(1));
    root.Add("texture",    Value::MakeString(texture.ToString()));
    root.Add("cellPixelW", Value::MakeNumber(cellPixelW));
    root.Add("cellPixelH", Value::MakeNumber(cellPixelH));
    Value arr = Value::MakeArray();
    for (int t : mask) arr.Push(Value::MakeNumber(t));
    root.Add("mask", std::move(arr));
    return root;
}

bool AutotileAsset::LoadFromFile(const std::string& path) {
    std::string text;
    if (!ReadFileText(path, text)) {
        NARAGAME_ERROR("Autotile::Load: cannot read '%s'", path.c_str());
        return false;
    }
    std::string err;
    const Value root = NaraGame::MiniJson::Parse(text, &err);
    if (!root.IsObject()) {
        NARAGAME_ERROR("Autotile::Load: '%s' parse failed: %s",
                       path.c_str(), err.c_str());
        return false;
    }
    FromJson(root);
    return true;
}

bool AutotileAsset::SaveToFile(const std::string& path) const {
    const std::string text = NaraGame::MiniJson::Write(ToJson());
    if (!WriteFileText(path, text)) {
        NARAGAME_ERROR("Autotile::Save: cannot write '%s'", path.c_str());
        return false;
    }
    return true;
}

} // namespace SceneEditor
