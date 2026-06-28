#include "Component.h"

#include "AnimatorComponent.h"
#include "BoxColliderComponent.h"
#include "CapsuleColliderComponent.h"
#include "CircleColliderComponent.h"
#include "PolygonColliderComponent.h"
#include "Rigidbody2DComponent.h"
#include "ScriptComponent.h"
#include "SpriteRendererComponent.h"
#include "TilemapComponent.h"
#include "TransformComponent.h"

namespace SceneEditor {

ComponentRegistry& ComponentRegistry::Get() {
    static ComponentRegistry instance;
    return instance;
}

void ComponentRegistry::Register(std::string typeName, Factory factory) {
    m_factories[std::move(typeName)] = std::move(factory);
}

std::unique_ptr<Component> ComponentRegistry::Create(const std::string& typeName) const {
    auto it = m_factories.find(typeName);
    if (it == m_factories.end()) return nullptr;
    return (it->second)();
}

bool ComponentRegistry::Has(const std::string& typeName) const {
    return m_factories.find(typeName) != m_factories.end();
}

std::vector<std::string> ComponentRegistry::TypeNames() const {
    std::vector<std::string> out;
    out.reserve(m_factories.size());
    for (const auto& [k, v] : m_factories) out.push_back(k);
    return out;
}

void RegisterBuiltinComponents() {
    auto& r = ComponentRegistry::Get();
    if (r.Has(TransformComponent::StaticTypeName())) return;  // idempotent
    r.Register(TransformComponent::StaticTypeName(),
               [] { return std::make_unique<TransformComponent>(); });
    r.Register(SpriteRendererComponent::StaticTypeName(),
               [] { return std::make_unique<SpriteRendererComponent>(); });
    r.Register(TilemapComponent::StaticTypeName(),
               [] { return std::make_unique<TilemapComponent>(); });
    r.Register(AnimatorComponent::StaticTypeName(),
               [] { return std::make_unique<AnimatorComponent>(); });
    r.Register(ScriptComponent::StaticTypeName(),
               [] { return std::make_unique<ScriptComponent>(); });
    r.Register(BoxColliderComponent::StaticTypeName(),
               [] { return std::make_unique<BoxColliderComponent>(); });
    r.Register(CircleColliderComponent::StaticTypeName(),
               [] { return std::make_unique<CircleColliderComponent>(); });
    r.Register(CapsuleColliderComponent::StaticTypeName(),
               [] { return std::make_unique<CapsuleColliderComponent>(); });
    r.Register(PolygonColliderComponent::StaticTypeName(),
               [] { return std::make_unique<PolygonColliderComponent>(); });
    r.Register(Rigidbody2DComponent::StaticTypeName(),
               [] { return std::make_unique<Rigidbody2DComponent>(); });
}

} // namespace SceneEditor
