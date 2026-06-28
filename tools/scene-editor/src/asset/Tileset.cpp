#include "Tileset.h"

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

void TilesetAsset::FromJson(const Value& root) {
    texture    = AssetGuid::FromString(root.GetString("texture"));
    cellPixelW = root.GetInt("cellPixelW", 16);
    cellPixelH = root.GetInt("cellPixelH", 16);
    tiles.clear();
    const Value* arr = root.Find("tiles");
    if (!arr || !arr->IsArray()) return;
    for (const Value& t : arr->arrayValue) {
        if (!t.IsObject()) continue;
        const int idx = t.GetInt("index", -1);
        if (idx < 0) continue;
        TilesetTile td;
        td.collides  = t.GetBool("collides",  false);
        td.isTrigger = t.GetBool("isTrigger", false);
        const std::string shapeStr = t.GetString("shape", "Box");
        if      (shapeStr == "OneWay")          td.shape = TileColliderShape::OneWay;
        else if (shapeStr == "SlopeRight")      td.shape = TileColliderShape::SlopeRight;
        else if (shapeStr == "SlopeLeft")       td.shape = TileColliderShape::SlopeLeft;
        else if (shapeStr == "SlopeRightHalf")  td.shape = TileColliderShape::SlopeRightHalf;
        else if (shapeStr == "SlopeLeftHalf")   td.shape = TileColliderShape::SlopeLeftHalf;
        else                                     td.shape = TileColliderShape::Box;
        tiles[idx] = td;
    }
}

Value TilesetAsset::ToJson() const {
    Value root = Value::MakeObject();
    root.Add("schema",     Value::MakeNumber(1));
    root.Add("texture",    Value::MakeString(texture.ToString()));
    root.Add("cellPixelW", Value::MakeNumber(cellPixelW));
    root.Add("cellPixelH", Value::MakeNumber(cellPixelH));
    Value arr = Value::MakeArray();
    for (const auto& kv : tiles) {
        Value t = Value::MakeObject();
        t.Add("index",     Value::MakeNumber(kv.first));
        t.Add("collides",  Value::MakeBool(kv.second.collides));
        t.Add("isTrigger", Value::MakeBool(kv.second.isTrigger));
        const char* shapeStr = "Box";
        switch (kv.second.shape) {
            case TileColliderShape::OneWay:         shapeStr = "OneWay";         break;
            case TileColliderShape::SlopeRight:     shapeStr = "SlopeRight";     break;
            case TileColliderShape::SlopeLeft:      shapeStr = "SlopeLeft";      break;
            case TileColliderShape::SlopeRightHalf: shapeStr = "SlopeRightHalf"; break;
            case TileColliderShape::SlopeLeftHalf:  shapeStr = "SlopeLeftHalf";  break;
            case TileColliderShape::Box:            default:                     break;
        }
        t.Add("shape", Value::MakeString(shapeStr));
        arr.Push(std::move(t));
    }
    root.Add("tiles", std::move(arr));
    return root;
}

bool TilesetAsset::LoadFromFile(const std::string& path) {
    std::string text;
    if (!ReadFileText(path, text)) {
        NARAGAME_ERROR("Tileset::Load: cannot read '%s'", path.c_str());
        return false;
    }
    std::string err;
    const Value root = NaraGame::MiniJson::Parse(text, &err);
    if (!root.IsObject()) {
        NARAGAME_ERROR("Tileset::Load: '%s' parse failed: %s",
                       path.c_str(), err.c_str());
        return false;
    }
    FromJson(root);
    return true;
}

bool TilesetAsset::SaveToFile(const std::string& path) const {
    const std::string text = NaraGame::MiniJson::Write(ToJson());
    if (!WriteFileText(path, text)) {
        NARAGAME_ERROR("Tileset::Save: cannot write '%s'", path.c_str());
        return false;
    }
    return true;
}

} // namespace SceneEditor
