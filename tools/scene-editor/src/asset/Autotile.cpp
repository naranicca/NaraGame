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

int AutotileAsset::Canonicalize8bit(int m) {
    // Bits: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW.
    if (!((m & 1)  && (m & 4)))  m &= ~2;    // NE requires N + E
    if (!((m & 4)  && (m & 16))) m &= ~8;    // SE requires E + S
    if (!((m & 16) && (m & 64))) m &= ~32;   // SW requires S + W
    if (!((m & 64) && (m & 1)))  m &= ~128;  // NW requires W + N
    return m;
}

void AutotileAsset::RebuildTileSet() {
    tileSet.clear();
    if (mode == Mode::Blob) {
        for (int m = 0; m < 256; ++m) {
            if (IsCanonical8bit(m)) tileSet.insert(blobMask[size_t(m)]);
        }
    } else {
        for (int t : mask) tileSet.insert(t);
    }
}

void AutotileAsset::FromJson(const Value& root) {
    texture    = AssetGuid::FromString(root.GetString("texture"));
    cellPixelW = root.GetInt("cellPixelW", 16);
    cellPixelH = root.GetInt("cellPixelH", 16);
    const std::string modeStr = root.GetString("mode", "Cardinal");
    mode = (modeStr == "Blob") ? Mode::Blob : Mode::Cardinal;
    mask.fill(0);
    blobMask.fill(0);
    if (mode == Mode::Cardinal) {
        const Value* arr = root.Find("mask");
        if (arr && arr->IsArray()) {
            const size_t n = std::min<size_t>(16, arr->arrayValue.size());
            for (size_t i = 0; i < n; ++i) {
                const Value& v = arr->arrayValue[i];
                mask[i] = v.IsNumber() ? int(v.numberValue) : 0;
            }
        }
    } else {
        // Blob: each entry is { "bits": <int>, "tile": <int> }. Only
        // canonical bit patterns are stored; non-canonical slots
        // shadow their canonical equivalent at expansion time.
        const Value* arr = root.Find("blobMask");
        if (arr && arr->IsArray()) {
            for (const Value& e : arr->arrayValue) {
                if (!e.IsObject()) continue;
                const int bits = e.GetInt("bits", -1);
                const int tile = e.GetInt("tile", 0);
                if (bits < 0 || bits > 255) continue;
                blobMask[size_t(bits)] = tile;
            }
        }
        // Shadow non-canonical slots with their canonical entries so
        // direct lookup blobMask[m] always returns the right tile.
        for (int m = 0; m < 256; ++m) {
            if (!IsCanonical8bit(m)) {
                blobMask[size_t(m)] = blobMask[size_t(Canonicalize8bit(m))];
            }
        }
    }
    RebuildTileSet();
}

Value AutotileAsset::ToJson() const {
    Value root = Value::MakeObject();
    root.Add("schema",     Value::MakeNumber(1));
    root.Add("mode",       Value::MakeString(
        mode == Mode::Blob ? "Blob" : "Cardinal"));
    root.Add("texture",    Value::MakeString(texture.ToString()));
    root.Add("cellPixelW", Value::MakeNumber(cellPixelW));
    root.Add("cellPixelH", Value::MakeNumber(cellPixelH));
    if (mode == Mode::Blob) {
        Value arr = Value::MakeArray();
        for (int m = 0; m < 256; ++m) {
            if (!IsCanonical8bit(m)) continue;
            Value e = Value::MakeObject();
            e.Add("bits", Value::MakeNumber(m));
            e.Add("tile", Value::MakeNumber(blobMask[size_t(m)]));
            arr.Push(std::move(e));
        }
        root.Add("blobMask", std::move(arr));
    } else {
        Value arr = Value::MakeArray();
        for (int t : mask) arr.Push(Value::MakeNumber(t));
        root.Add("mask", std::move(arr));
    }
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
