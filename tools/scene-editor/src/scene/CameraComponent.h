#pragma once

#include "Component.h"

#include <NaraGame/assets/MiniJson.h>

namespace SceneEditor {

// M9c11 game-runtime camera. Attaches to whichever entity is the
// player's viewpoint (player character, follow rig, cutscene
// anchor). The runtime walks the scene every frame, picks the
// highest-priority enabled CameraComponent, and uses that
// entity's Transform position + this component's zoom as the
// viewport state. Ties break in scene-traversal order.
//
// `zoom` is in pixels per world unit (matches the editor's
// ViewportCamera convention). The editor's viewport camera
// stays driven by user pan / zoom in editor mode; standalone
// runs override it with the active CameraComponent on every
// frame so a scripted camera shake / follow lerp surfaces
// immediately. If no CameraComponent exists the standalone
// path falls back to the last user-set view.
class CameraComponent final : public Component {
public:
    // Pixels per world unit. Default matches the editor's
    // ViewportCamera.zoom default so dragging a fresh camera
    // in keeps the same scale the user saw in the editor.
    float zoom     = 48.0f;
    // Higher wins when multiple cameras coexist. Useful for
    // a UI / cutscene camera that should grab the viewport
    // when enabled, then fall back to the player camera once
    // it's disabled.
    int   priority = 0;

    static const char* StaticTypeName() { return "Camera"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
