#pragma once

#include <NaraGame/assets/MiniJson.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace SceneEditor {

// Base class for everything that lives on an Entity. Each concrete
// component declares a stable TypeName string -- that's the JSON
// discriminator and the registry key. Editor inspector code drives all
// edits through the same DrawInspector path (added in a follow-up
// commit) so a new component type drops in with no extra inspector
// plumbing once it's registered.
class Component {
public:
    virtual ~Component() = default;

    virtual const char* TypeName() const = 0;
    virtual void Serialize(NaraGame::MiniJson::Value& out) const = 0;
    virtual void Deserialize(const NaraGame::MiniJson::Value& obj) = 0;
};

// Type-name -> factory registry so the scene loader can build the right
// concrete Component from a JSON "type" string. RegisterBuiltinTypes()
// is called once during editor startup; user-script components will
// register themselves through the same API in a later milestone.
class ComponentRegistry {
public:
    using Factory = std::function<std::unique_ptr<Component>()>;

    static ComponentRegistry& Get();

    void Register(std::string typeName, Factory factory);
    std::unique_ptr<Component> Create(const std::string& typeName) const;
    bool Has(const std::string& typeName) const;
    std::vector<std::string> TypeNames() const;

private:
    std::unordered_map<std::string, Factory> m_factories;
};

void RegisterBuiltinComponents();

} // namespace SceneEditor
