#pragma once

#include "Component.h"

#include <NaraGame/math/Vec3.h>

namespace SceneEditor {

// First-class component every entity ships with. Holds a 3D position +
// uniform 3-component scale, but the rotation stays as a single z-axis
// degree value while the editor is 2D-first; a 3D milestone will
// promote it to a Quat (or Vec3 euler) without rotating the JSON
// schema.
class TransformComponent final : public Component {
public:
    NaraGame::Vec3 position{0.0f, 0.0f, 0.0f};
    float          rotation = 0.0f;  // degrees, z-axis
    NaraGame::Vec3 scale{1.0f, 1.0f, 1.0f};

    static const char* StaticTypeName() { return "Transform"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
