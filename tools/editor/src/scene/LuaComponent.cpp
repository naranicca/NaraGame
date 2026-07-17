#include "LuaComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

// Serialise the whole MiniJson fields object verbatim under
// the "fields" key. The component's typeName is already on the
// outer envelope (Scene::Serialize writes "type": "<TypeName>")
// so a re-load picks it up before this Deserialize call runs.
void LuaComponent::Serialize(Value& out) const {
    out.Add("fields", m_fields);
}

void LuaComponent::Deserialize(const Value& obj) {
    if (const Value* f = obj.Find("fields")) {
        if (f->IsObject()) {
            m_fields = *f;
        }
    }
}

} // namespace SceneEditor
