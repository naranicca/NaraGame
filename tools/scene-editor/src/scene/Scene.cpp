#include "Scene.h"

#include "Component.h"
#include "TransformComponent.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/core/Log.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace SceneEditor {

using NaraGame::MiniJson::Value;

Scene::Scene() = default;

EntityId Scene::CreateEntity(std::string name, EntityId parent) {
    auto e   = std::make_unique<Entity>();
    e->id    = m_nextId++;
    e->name  = std::move(name);
    e->parent = parent;
    // Every entity ships with a Transform; the rest of the editor + any
    // runtime systems can assume it's always there.
    e->components.push_back(std::make_unique<TransformComponent>());

    const EntityId id = e->id;
    if (parent != kInvalidEntityId) {
        if (Entity* p = Get(parent)) p->children.push_back(id);
        else NARAGAME_WARN("Scene::CreateEntity: parent %llu missing", parent);
    } else {
        m_roots.push_back(id);
    }
    m_entities.emplace(id, std::move(e));
    m_dirty = true;
    return id;
}

Entity* Scene::Get(EntityId id) {
    auto it = m_entities.find(id);
    return (it == m_entities.end()) ? nullptr : it->second.get();
}

const Entity* Scene::Get(EntityId id) const {
    auto it = m_entities.find(id);
    return (it == m_entities.end()) ? nullptr : it->second.get();
}

void Scene::DestroyEntity(EntityId id) {
    Entity* e = Get(id);
    if (!e) return;
    // Destroy children first so we don't leave dangling parent IDs.
    // Copy the child list since destruction mutates it.
    const std::vector<EntityId> children = e->children;
    for (EntityId c : children) DestroyEntity(c);

    // Detach from parent / root list.
    if (e->parent != kInvalidEntityId) {
        if (Entity* p = Get(e->parent)) {
            auto& cs = p->children;
            cs.erase(std::remove(cs.begin(), cs.end(), id), cs.end());
        }
    } else {
        m_roots.erase(std::remove(m_roots.begin(), m_roots.end(), id),
                      m_roots.end());
    }
    m_entities.erase(id);
    m_dirty = true;
}

void Scene::Reparent(EntityId child, EntityId newParent) {
    Entity* e = Get(child);
    if (!e || e->parent == newParent) return;
    // Don't allow reparenting under one's own descendant; walk up the
    // proposed parent chain and refuse if we hit `child`.
    EntityId cursor = newParent;
    while (cursor != kInvalidEntityId) {
        if (cursor == child) return;
        const Entity* up = Get(cursor);
        if (!up) break;
        cursor = up->parent;
    }
    // Detach.
    if (e->parent != kInvalidEntityId) {
        if (Entity* p = Get(e->parent)) {
            auto& cs = p->children;
            cs.erase(std::remove(cs.begin(), cs.end(), child), cs.end());
        }
    } else {
        m_roots.erase(std::remove(m_roots.begin(), m_roots.end(), child),
                      m_roots.end());
    }
    // Attach.
    e->parent = newParent;
    if (newParent != kInvalidEntityId) {
        if (Entity* p = Get(newParent)) p->children.push_back(child);
    } else {
        m_roots.push_back(child);
    }
    m_dirty = true;
}

void Scene::Clear() {
    m_entities.clear();
    m_roots.clear();
    m_nextId = 1;
    m_dirty  = false;
}

// ---------------------------------------------------------------------------
// JSON I/O
// ---------------------------------------------------------------------------

namespace {

bool ReadFileText(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool WriteFileText(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << content;
    return out.good();
}

Value EntityToJson(const Entity& e) {
    Value o = Value::MakeObject();
    o.Add("id",      Value::MakeNumber(double(e.id)));
    o.Add("name",    Value::MakeString(e.name));
    o.Add("enabled", Value::MakeBool(e.enabled));
    o.Add("parent",  Value::MakeNumber(double(e.parent)));
    Value comps = Value::MakeArray();
    for (const auto& c : e.components) {
        Value co = Value::MakeObject();
        co.Add("type", Value::MakeString(c->TypeName()));
        c->Serialize(co);
        comps.Push(std::move(co));
    }
    o.Add("components", std::move(comps));
    return o;
}

} // namespace

bool Scene::Save(const std::string& path) const {
    Value root = Value::MakeObject();
    root.Add("schema",    Value::MakeNumber(1));
    root.Add("sceneName", Value::MakeString(m_name));
    root.Add("nextId",    Value::MakeNumber(double(m_nextId)));
    Value entities = Value::MakeArray();
    // Iterate in stable order (by id) so the file diff stays sane.
    std::vector<EntityId> ids;
    ids.reserve(m_entities.size());
    for (const auto& [id, _] : m_entities) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (EntityId id : ids) {
        const Entity* e = Get(id);
        if (e) entities.Push(EntityToJson(*e));
    }
    root.Add("entities", std::move(entities));

    const std::string text = NaraGame::MiniJson::Write(root);
    if (!WriteFileText(path, text)) {
        NARAGAME_ERROR("Scene::Save: cannot write '%s'", path.c_str());
        return false;
    }
    NARAGAME_INFO("Scene::Save: '%s' written (%zu entities)",
                  path.c_str(), m_entities.size());
    return true;
}

bool Scene::Load(const std::string& path) {
    std::string text;
    if (!ReadFileText(path, text)) {
        NARAGAME_ERROR("Scene::Load: cannot read '%s'", path.c_str());
        return false;
    }
    std::string err;
    const Value root = NaraGame::MiniJson::Parse(text, &err);
    if (!root.IsObject()) {
        NARAGAME_ERROR("Scene::Load: '%s' parse failed: %s",
                       path.c_str(), err.c_str());
        return false;
    }

    Clear();
    m_name = root.GetString("sceneName", "Untitled");

    const Value* arr = root.Find("entities");
    if (arr && arr->IsArray()) {
        for (const Value& eo : arr->arrayValue) {
            auto e = std::make_unique<Entity>();
            e->id      = EntityId(eo.GetNumber("id", 0));
            e->name    = eo.GetString("name", "Entity");
            e->enabled = eo.GetBool("enabled", true);
            e->parent  = EntityId(eo.GetNumber("parent", 0));
            if (e->id == kInvalidEntityId) {
                NARAGAME_WARN("Scene::Load: '%s' entry missing id, skipped",
                              path.c_str());
                continue;
            }
            const Value* comps = eo.Find("components");
            if (comps && comps->IsArray()) {
                for (const Value& co : comps->arrayValue) {
                    const std::string& type = co.GetString("type");
                    auto comp = ComponentRegistry::Get().Create(type);
                    if (!comp) {
                        NARAGAME_WARN("Scene::Load: unknown component '%s' on "
                                      "entity %llu",
                                      type.c_str(), e->id);
                        continue;
                    }
                    comp->Deserialize(co);
                    e->components.push_back(std::move(comp));
                }
            }
            // Guarantee a Transform even if the file lacked one.
            if (!e->GetComponent<TransformComponent>()) {
                e->components.push_back(std::make_unique<TransformComponent>());
            }
            m_nextId = std::max(m_nextId, e->id + 1);
            m_entities.emplace(e->id, std::move(e));
        }
    }
    // Stitch parent / child lists from the parent field.
    for (auto& [id, ent] : m_entities) {
        if (ent->parent == kInvalidEntityId) {
            m_roots.push_back(id);
        } else if (Entity* p = Get(ent->parent)) {
            p->children.push_back(id);
        } else {
            NARAGAME_WARN("Scene::Load: entity %llu references missing "
                          "parent %llu; treating as root",
                          id, ent->parent);
            ent->parent = kInvalidEntityId;
            m_roots.push_back(id);
        }
    }
    // Roots sorted by id for stable display.
    std::sort(m_roots.begin(), m_roots.end());

    // The nextId stored in the file wins if it's larger -- protects
    // against an external editor inserting entities with low ids.
    const auto savedNext = EntityId(root.GetNumber("nextId", double(m_nextId)));
    m_nextId = std::max(m_nextId, savedNext);

    m_dirty = false;
    NARAGAME_INFO("Scene::Load: '%s' loaded (%zu entities)",
                  path.c_str(), m_entities.size());
    return true;
}

} // namespace SceneEditor
