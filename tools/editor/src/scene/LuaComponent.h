#pragma once

#include "Component.h"

#include <NaraGame/assets/MiniJson.h>

#include <string>

namespace SceneEditor {

// M11c3 generic Lua-defined component. The data model is a JSON
// object whose keys + values come from the script side (typically
// via `component.register("Health", {max=100, current=100})` + a
// later `scene.add_component(entity, "Health")` that copies the
// registered defaults in).
//
// Custom-typed components don't get their own C++ class; they
// reuse this one with a `typeName` field that mirrors what
// component.register specified. Scene serialisation reads the
// type back exactly so a custom Health component round-trips
// through the .nscene file the same way a built-in does.
//
// The runtime treats this as "data only" -- there is no built-in
// editor inspector for the contents (a generic key/value editor
// would tempt the user to invent ad-hoc UI surfaces; we leave
// that to the script side). Scripts mutate the data via
// component.get / component.set which copy the table in + out.
class LuaComponent final : public Component {
public:
    LuaComponent() = default;
    explicit LuaComponent(std::string typeName)
        : m_typeName(std::move(typeName)) {}

    const char* TypeName() const override {
        return m_typeName.empty() ? "Custom" : m_typeName.c_str();
    }
    // Stable string for things that need StaticTypeName(); the
    // real type name is dynamic per-instance via TypeName(). This
    // is only used by the C++ registry's bookkeeping, never read
    // by the deserialiser.
    static const char* StaticTypeName() { return "_LuaComponent"; }

    // Public so ScriptHost factories + the scene loader can mint
    // a LuaComponent with the right type before the Deserialize
    // call pulls in the field data.
    void SetTypeName(std::string n) { m_typeName = std::move(n); }
    const std::string& Type() const  { return m_typeName; }

    // Field data lives as a MiniJson object; values are number /
    // string / bool / nested object / array. The Lua bindings
    // convert between this + a Lua table on the way in / out.
    NaraGame::MiniJson::Value& Fields()             { return m_fields; }
    const NaraGame::MiniJson::Value& Fields() const { return m_fields; }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;

private:
    std::string                m_typeName;
    NaraGame::MiniJson::Value  m_fields = NaraGame::MiniJson::Value::MakeObject();
};

} // namespace SceneEditor
