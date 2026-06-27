#pragma once

#include "Entity.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace SceneEditor {

// In-memory editor scene. Holds the entity graph + their components and
// round-trips to a `.nscene` JSON file via Save / Load. There's exactly
// one Scene live in the editor at a time; EditorApp swaps it on
// File > New Scene / Open Scene.
class Scene {
public:
    Scene();

    EntityId CreateEntity(std::string name = "Entity",
                          EntityId parent = kInvalidEntityId);
    void     DestroyEntity(EntityId id);  // recurses into children

    Entity*       Get(EntityId id);
    const Entity* Get(EntityId id) const;

    // Top-level entities (parent == 0). Stable insertion order.
    const std::vector<EntityId>& Roots() const { return m_roots; }

    void Reparent(EntityId child, EntityId newParent);

    // Disk I/O. Save writes pretty JSON; Load wipes the current state
    // and rebuilds from the file. Returns false on read / parse failure
    // (the scene is left in whatever state it was before the call).
    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

    // In-memory round-trip used by the editor's undo system. Same JSON
    // shape Save / Load produce; failures in DeserializeFromString leave
    // the scene unchanged the same way Load does on parse error.
    std::string SerializeToString() const;
    bool        DeserializeFromString(const std::string& json);

    void Clear();

    const std::string& Name() const { return m_name; }
    void               SetName(std::string n) { m_name = std::move(n); }

    bool IsDirty() const { return m_dirty; }
    void MarkDirty()     { m_dirty = true; }
    void ClearDirty()    { m_dirty = false; }

private:
    EntityId m_nextId = 1;
    std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
    std::vector<EntityId> m_roots;
    std::string           m_name = "Untitled";
    bool                  m_dirty = false;
};

} // namespace SceneEditor
