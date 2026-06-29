#pragma once

#include "../asset/AssetGuid.h"
#include "Component.h"

#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/rhi/RHITypes.h>

#include <string>

namespace SceneEditor {

// Sprite draw shape. M7c16 ships all three: Simple = single quad,
// Sliced = 9-zone stretch (corners + edges + centre), Tiled = 9-zone
// repeat. Each mode honours the Transform's rotation as a single
// rigid quad. Sliced + Tiled read border widths from the texture's
// import settings (or per-sub-sprite when subSprite is set).
enum class SpriteDrawMode { Simple = 0, Sliced = 1, Tiled = 2 };

// Renders an asset-backed 2D sprite at the entity's transform. References
// the sprite asset by GUID -- the editor's AssetDatabase resolves that
// to a runtime texture pointer on first draw and caches it for the rest
// of the session.
class SpriteRendererComponent final : public Component {
public:
    AssetGuid       sprite;                       // asset GUID; invalid = unassigned
    // Named slice from the texture's SpriteSheet. Empty string means
    // "draw the whole texture" (the original behaviour); a non-empty
    // name resolves through the texture's importSettings.spriteSheet
    // at draw time. Names survive reorders + edits in the Sprite
    // Editor, but a rename leaves an orphan reference here -- the
    // inspector flags those so the user can pick a live name.
    std::string     subSprite;
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
