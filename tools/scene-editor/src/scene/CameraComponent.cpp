#include "CameraComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void CameraComponent::Serialize(Value& out) const {
    out.Add("zoom",     Value::MakeNumber(zoom));
    out.Add("priority", Value::MakeNumber(priority));
}

void CameraComponent::Deserialize(const Value& obj) {
    zoom     = float(obj.GetNumber("zoom",     48.0));
    priority = obj.GetInt("priority", 0);
    if (zoom < 0.01f) zoom = 0.01f;
}

} // namespace SceneEditor
