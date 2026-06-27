#include "TilemapComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void TilemapComponent::Serialize(Value& out) const {
    out.Add("tileset",      Value::MakeString(tileset.ToString()));
    out.Add("cellPixelW",   Value::MakeNumber(cellPixelW));
    out.Add("cellPixelH",   Value::MakeNumber(cellPixelH));
    out.Add("sortingLayer", Value::MakeString(sortingLayer));
    out.Add("orderInLayer", Value::MakeNumber(orderInLayer));
    // Each cell is a [gx, gy, tile] triple. Sparse storage keeps the
    // scene file small even for very large maps.
    Value arr = Value::MakeArray();
    for (const auto& kv : cells) {
        Value t = Value::MakeArray();
        t.Push(Value::MakeNumber(kv.first.first));
        t.Push(Value::MakeNumber(kv.first.second));
        t.Push(Value::MakeNumber(kv.second));
        arr.Push(std::move(t));
    }
    out.Add("cells", std::move(arr));
}

void TilemapComponent::Deserialize(const Value& obj) {
    tileset      = AssetGuid::FromString(obj.GetString("tileset"));
    cellPixelW   = obj.GetInt("cellPixelW", 16);
    cellPixelH   = obj.GetInt("cellPixelH", 16);
    sortingLayer = obj.GetString("sortingLayer", "Default");
    orderInLayer = obj.GetInt("orderInLayer", 0);
    cells.clear();
    const Value* arr = obj.Find("cells");
    if (!arr || !arr->IsArray()) return;
    for (const Value& v : arr->arrayValue) {
        if (!v.IsArray() || v.arrayValue.size() < 3) continue;
        auto pick = [&](size_t i) {
            const Value& x = v.arrayValue[i];
            return x.IsNumber() ? int(x.numberValue) : 0;
        };
        cells[{pick(0), pick(1)}] = pick(2);
    }
}

} // namespace SceneEditor
