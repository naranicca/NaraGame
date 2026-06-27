#pragma once

#include "../asset/AssetGuid.h"
#include "Component.h"

#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/rhi/RHITypes.h>

#include <string>

namespace SceneEditor {

// Sprite draw shape. Only Simple is wired into the viewport renderer
// for M2c1; Sliced (9-slice) and Tiled arrive with the sprite editor in
// M3 but the enum + sliced/tiled fields are persisted now so existing
// scenes don't need a schema migration when the renderer catches up.
enum class SpriteDrawMode { Simple = 0, Sliced = 1, Tiled = 2 };

// Renders an asset-backed 2D sprite at the entity's transform. References
// the sprite asset by GUID -- the editor's AssetDatabase resolves that
// to a runtime texture pointer on first draw and caches it for the rest
// of the session.
class SpriteRendererComponent final : public Component {
public:
    AssetGuid       sprite;                       // asset GUID; invalid = unassigned
    NaraGame::Color color = NaraGame::Color::White();
    bool            flipX = false;
    bool            flipY = false;
    SpriteDrawMode  drawMode = SpriteDrawMode::Simple;
    NaraGame::Vec2  size = {1.0f, 1.0f};          // world-units; used by Sliced/Tiled
    std::string     sortingLayer = "Default";
    int             orderInLayer = 0;

    static const char* StaticTypeName() { return "SpriteRenderer"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
