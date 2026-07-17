#pragma once

#include "Component.h"

#include <memory>
#include <string>
#include <vector>

namespace SceneEditor {

// Stable per-scene identifier. 0 means "invalid / no parent"; live
// entities get a positive value handed out by Scene::CreateEntity. The
// numeric form serializes straight as a JSON number; no UUID rigging
// yet -- entity refs cross only inside one scene file.
using EntityId = std::uint64_t;
constexpr EntityId kInvalidEntityId = 0;

// One node in the scene graph. The editor leans on the GameObject side
// of Unity's GameObject + Component pair: Entity owns metadata (name /
// enabled / parent / children) and a list of owning Components, none
// of which are required up front -- Scene::CreateEntity attaches a
// Transform automatically because every node needs one.
struct Entity {
    EntityId               id      = kInvalidEntityId;
    std::string            name;
    bool                   enabled = true;
    EntityId               parent  = kInvalidEntityId;
    std::vector<EntityId>  children;
    std::vector<std::unique_ptr<Component>> components;

    // Lookup helpers. Returns the first component whose runtime type
    // matches T; nullptr if none. We intentionally don't sort or index
    // components -- per-entity lists are short (<10) and a linear walk
    // is fine.
    template <class T>
    T* GetComponent() {
        for (auto& c : components) {
            if (T* hit = dynamic_cast<T*>(c.get())) return hit;
        }
        return nullptr;
    }

    template <class T>
    const T* GetComponent() const {
        for (const auto& c : components) {
            if (const T* hit = dynamic_cast<const T*>(c.get())) return hit;
        }
        return nullptr;
    }
};

} // namespace SceneEditor
