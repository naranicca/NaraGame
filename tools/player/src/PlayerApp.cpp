#include <NaraGame/core/Application.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/math/Mat4.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/TextureRegion.h>
#include <NaraGamePlayer/ProjectPlayer.h>

#include "asset/Animation.h"
#include "asset/AnimatorController.h"
#include "asset/Autotile.h"
#include "asset/Pak.h"
#include "asset/Tileset.h"
#include "asset/AssetDatabase.h"
#include "asset/TextureImportSettings.h"
#include "project/Project.h"
#include "scene/CameraComponent.h"
#include "scene/AnimatorComponent.h"
#include "scene/BoxColliderComponent.h"
#include "scene/CircleColliderComponent.h"
#include "scene/CapsuleColliderComponent.h"
#include "scene/EdgeColliderComponent.h"
#include "scene/PolygonColliderComponent.h"
#include "scene/Component.h"
#include "scene/Scene.h"
#include "scene/ScriptComponent.h"
#include "scene/Rigidbody2DComponent.h"
#include "scene/SpriteRendererComponent.h"
#include "scene/TransformComponent.h"
#include "scene/TilemapComponent.h"
#include "scripting/ScriptHost.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct WorldTransform {
    NaraGame::Vec2 position{0.0f, 0.0f};
    NaraGame::Vec2 scale{1.0f, 1.0f};
    float rotation = 0.0f;
};

struct SpriteDraw {
    const SceneEditor::Entity* entity = nullptr;
    const SceneEditor::SpriteRendererComponent* sprite = nullptr;
    const SceneEditor::TilemapComponent* tilemap = nullptr;
    WorldTransform world;
    int layer = 0;
};

class ProjectPlayer final : public NaraGame::Application {
public:
    explicit ProjectPlayer(std::string projectRoot, int maxFrames)
        : Application({"NaraGame Player", 1280, 720, true}),
          m_projectRoot(std::move(projectRoot)), m_maxFrames(maxFrames) {}

protected:
    void OnStart() override {
        SceneEditor::RegisterBuiltinComponents();
        if (!m_project.Open(m_projectRoot)) {
            NARAGAME_ERROR("Player: cannot open project at '%s'",
                           m_projectRoot.c_str());
            Close();
            return;
        }
        m_assets.Open(m_project);
        m_scripts.BindScene(&m_scene);
        m_scripts.BindAssetDatabase(&m_assets);
        for (const SceneEditor::AssetEntry& asset : m_assets.Entries()) {
            if (asset.typeName == "Script") {
                m_scripts.LoadFile(m_assets.AssetsRoot() + "/" + asset.relPath,
                                   asset.relPath);
            }
        }
        const std::string sceneRel = m_project.MainScenePath();
        if (sceneRel.empty() ||
            !m_scene.Load(m_project.AssetsDir() + "/" + sceneRel)) {
            NARAGAME_ERROR("Player: main scene is missing or could not be loaded");
            Close();
            return;
        }
        m_renderer.Init(GetDevice());
        m_ready = true;
        NARAGAME_INFO("Player: loaded %s", sceneRel.c_str());
    }

    void OnUpdate(float dt) override {
        if (!m_ready) return;
        if (m_maxFrames > 0 && ++m_frames >= m_maxFrames) Close();
        TickPhysics(dt);
        m_scripts.Tick(dt);
        std::vector<SceneEditor::EntityId> ids;
        for (SceneEditor::EntityId root : m_scene.Roots()) CollectIds(root, ids);
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* entity = m_scene.Get(id);
            if (entity == nullptr) continue;
            auto* script = entity->GetComponent<SceneEditor::ScriptComponent>();
            if (script == nullptr) continue;
            if (entity->enabled != script->lastSeenEnabled) {
                if (entity->enabled)
                    m_scripts.CallOnEnable(entity->name, script->entryFunction);
                else
                    m_scripts.CallOnDisable(entity->name, script->entryFunction);
                script->lastSeenEnabled = entity->enabled;
            }
            if (!entity->enabled) continue;
            if (!script->started) {
                m_scripts.CallOnStart(entity->name, script->entryFunction);
                script->started = true;
            }
            m_scripts.CallOnUpdate(entity->name, dt, script->entryFunction);
        }
        if (m_scripts.HasPendingSceneLoad()) LoadPendingScene();
        TickAnimators(dt);
    }

    void OnRender() override {
        GetDevice().Clear({0.08f, 0.09f, 0.12f, 1.0f});
        if (!m_ready) return;

        NaraGame::Vec2 cameraPosition{0.0f, 0.0f};
        float pixelsPerUnit = 48.0f;
        int cameraPriority = -2147483647;
        std::vector<SpriteDraw> draws;
        for (SceneEditor::EntityId id : m_scene.Roots()) {
            Collect(id, {}, true, draws, cameraPosition,
                    pixelsPerUnit, cameraPriority);
        }
        std::stable_sort(draws.begin(), draws.end(),
            [](const SpriteDraw& a, const SpriteDraw& b) {
                if (a.layer != b.layer) return a.layer < b.layer;
                const int ao = a.sprite ? a.sprite->orderInLayer
                                        : a.tilemap->orderInLayer;
                const int bo = b.sprite ? b.sprite->orderInLayer
                                        : b.tilemap->orderInLayer;
                return ao < bo;
            });

        const float halfW = float(GetWindow().Width()) /
                            (2.0f * pixelsPerUnit);
        const float halfH = float(GetWindow().Height()) /
                            (2.0f * pixelsPerUnit);
        const NaraGame::Mat4 camera = NaraGame::mat4::Ortho(
            cameraPosition.x - halfW, cameraPosition.x + halfW,
            cameraPosition.y - halfH, cameraPosition.y + halfH,
            -1000.0f, 1000.0f);
        m_renderer.Begin(camera);
        for (const SpriteDraw& draw : draws) {
            if (draw.sprite) DrawSprite(draw);
            else DrawTilemap(draw);
        }
        m_renderer.End();
    }

    void OnStop() override {
        NotifyScriptsForDestroy();
        m_renderer.Shutdown();
        m_assets.Close();
        m_project.Close();
        SceneEditor::UnmountPak();
    }

    void OnDeviceLost() override {
        m_renderer.Shutdown();
        m_assets.Close();
    }
    void OnDeviceRestored() override {
        m_assets.Open(m_project);
        m_renderer.Init(GetDevice());
    }

private:
    struct Collider {
        enum class Kind { Box, Circle, Polygon } kind = Kind::Box;
        SceneEditor::EntityId id = 0;
        std::string name;
        std::string entry;
        std::string layer;
        bool scripted = false;
        bool trigger = false;
        bool oneWay = false;
        float minX = 0, maxX = 0, minY = 0, maxY = 0;
        float cx = 0, cy = 0, halfX = 0, halfY = 0, radius = 0;
        float cosR = 1.0f, sinR = 0.0f;
        std::vector<NaraGame::Vec2> vertices;
    };

    void TickPhysics(float dt) {
        dt = std::min(dt, 1.0f / 30.0f);
        constexpr int substeps = 4;
        for (int step = 0; step < substeps; ++step) {
            const float subDt = dt / float(substeps);
            std::vector<SceneEditor::EntityId> ids;
            for (SceneEditor::EntityId root : m_scene.Roots()) CollectIds(root, ids);
            for (SceneEditor::EntityId id : ids) {
                SceneEditor::Entity* entity = m_scene.Get(id);
                if (entity == nullptr || !entity->enabled) continue;
                auto* transform = entity->GetComponent<
                    SceneEditor::TransformComponent>();
                auto* body = entity->GetComponent<
                    SceneEditor::Rigidbody2DComponent>();
                if (transform == nullptr || body == nullptr) continue;
                body->velocity.x += body->gravity.x * subDt;
                body->velocity.y += body->gravity.y * subDt;
                const float damping = std::max(0.0f, 1.0f - body->drag * subDt);
                body->velocity.x *= damping;
                body->velocity.y *= damping;
                transform->position.x += body->velocity.x * subDt;
                transform->position.y += body->velocity.y * subDt;
            }
            ResolveCollisions();
        }
    }

    void ResolveCollisions() {
        std::vector<Collider> colliders;
        std::vector<SceneEditor::EntityId> ids;
        for (SceneEditor::EntityId root : m_scene.Roots()) CollectIds(root, ids);
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* entity = m_scene.Get(id);
            if (entity == nullptr || !entity->enabled) continue;
            auto* tr = entity->GetComponent<SceneEditor::TransformComponent>();
            if (tr == nullptr) continue;
            Collider c;
            c.id = id; c.name = entity->name;
            if (auto* script = entity->GetComponent<SceneEditor::ScriptComponent>()) {
                c.scripted = true; c.entry = script->entryFunction;
            }
            if (auto* box = entity->GetComponent<SceneEditor::BoxColliderComponent>()) {
                const float hx = std::abs(box->size.x * tr->scale.x) * 0.5f;
                const float hy = std::abs(box->size.y * tr->scale.y) * 0.5f;
                const float radians = tr->rotation *
                    3.14159265358979323846f / 180.0f;
                c.cosR = std::cos(radians); c.sinR = std::sin(radians);
                const float offsetX = box->offset.x * tr->scale.x;
                const float offsetY = box->offset.y * tr->scale.y;
                const float cx = tr->position.x +
                    c.cosR * offsetX - c.sinR * offsetY;
                const float cy = tr->position.y +
                    c.sinR * offsetX + c.cosR * offsetY;
                const float boundX = std::abs(c.cosR) * hx +
                                     std::abs(c.sinR) * hy;
                const float boundY = std::abs(c.sinR) * hx +
                                     std::abs(c.cosR) * hy;
                c.minX = cx - boundX; c.maxX = cx + boundX;
                c.minY = cy - boundY; c.maxY = cy + boundY;
                c.kind = Collider::Kind::Box;
                c.cx = cx; c.cy = cy; c.halfX = hx; c.halfY = hy;
                c.trigger = box->isTrigger; c.layer = box->layer;
                colliders.push_back(c);
            } else if (auto* circle = entity->GetComponent<
                           SceneEditor::CircleColliderComponent>()) {
                const float radius = circle->radius * std::max(
                    std::abs(tr->scale.x), std::abs(tr->scale.y));
                const float radians = tr->rotation *
                    3.14159265358979323846f / 180.0f;
                const float cosR = std::cos(radians), sinR = std::sin(radians);
                const float offsetX = circle->offset.x * tr->scale.x;
                const float offsetY = circle->offset.y * tr->scale.y;
                const float cx = tr->position.x + cosR * offsetX - sinR * offsetY;
                const float cy = tr->position.y + sinR * offsetX + cosR * offsetY;
                c.minX = cx - radius; c.maxX = cx + radius;
                c.minY = cy - radius; c.maxY = cy + radius;
                c.kind = Collider::Kind::Circle;
                c.cx = cx; c.cy = cy; c.radius = radius;
                c.trigger = circle->isTrigger; c.layer = circle->layer;
                colliders.push_back(c);
            } else if (auto* capsule = entity->GetComponent<
                           SceneEditor::CapsuleColliderComponent>()) {
                const float radians = tr->rotation *
                    3.14159265358979323846f / 180.0f;
                c.cosR = std::cos(radians); c.sinR = std::sin(radians);
                const float offsetX = capsule->offset.x * tr->scale.x;
                const float offsetY = capsule->offset.y * tr->scale.y;
                c.cx = tr->position.x + c.cosR * offsetX - c.sinR * offsetY;
                c.cy = tr->position.y + c.sinR * offsetX + c.cosR * offsetY;
                c.halfX = capsule->radius * std::abs(tr->scale.x);
                c.halfY = capsule->halfLength * std::abs(tr->scale.y);
                c.kind = Collider::Kind::Box;
                const float boundX = std::abs(c.cosR) * c.halfX +
                                     std::abs(c.sinR) * c.halfY;
                const float boundY = std::abs(c.sinR) * c.halfX +
                                     std::abs(c.cosR) * c.halfY;
                c.minX = c.cx - boundX; c.maxX = c.cx + boundX;
                c.minY = c.cy - boundY; c.maxY = c.cy + boundY;
                c.trigger = capsule->isTrigger; c.layer = capsule->layer;
                colliders.push_back(c);
                const float capDistance = capsule->halfLength *
                                          std::abs(tr->scale.y);
                const float capRadius = capsule->radius * std::max(
                    std::abs(tr->scale.x), std::abs(tr->scale.y));
                for (float sign : {-1.0f, 1.0f}) {
                    Collider cap = c;
                    cap.kind = Collider::Kind::Circle;
                    cap.radius = capRadius;
                    cap.cx = c.cx - c.sinR * capDistance * sign;
                    cap.cy = c.cy + c.cosR * capDistance * sign;
                    cap.minX = cap.cx - capRadius; cap.maxX = cap.cx + capRadius;
                    cap.minY = cap.cy - capRadius; cap.maxY = cap.cy + capRadius;
                    colliders.push_back(cap);
                }
            } else if (auto* edge = entity->GetComponent<
                           SceneEditor::EdgeColliderComponent>()) {
                if (edge->vertices.size() < 2) continue;
                const float radians = tr->rotation *
                    3.14159265358979323846f / 180.0f;
                const float transformCos = std::cos(radians);
                const float transformSin = std::sin(radians);
                const size_t segmentCount = edge->vertices.size() - 1 +
                    (edge->closed ? 1 : 0);
                for (size_t segment = 0; segment < segmentCount; ++segment) {
                    auto toWorld = [&](const NaraGame::Vec2& point) {
                        const float x = (point.x + edge->offset.x) * tr->scale.x;
                        const float y = (point.y + edge->offset.y) * tr->scale.y;
                        return NaraGame::Vec2{
                            tr->position.x + transformCos * x - transformSin * y,
                            tr->position.y + transformSin * x + transformCos * y};
                    };
                    const NaraGame::Vec2 p0 = toWorld(edge->vertices[segment]);
                    const NaraGame::Vec2 p1 = toWorld(
                        edge->vertices[(segment + 1) % edge->vertices.size()]);
                    const float dx = p1.x - p0.x, dy = p1.y - p0.y;
                    const float length = std::sqrt(dx * dx + dy * dy);
                    if (length <= 1e-6f) continue;
                    Collider part = c;
                    part.kind = Collider::Kind::Box;
                    part.cx = (p0.x + p1.x) * 0.5f;
                    part.cy = (p0.y + p1.y) * 0.5f;
                    part.halfX = length * 0.5f;
                    part.halfY = 0.01f;
                    part.cosR = dx / length; part.sinR = dy / length;
                    const float boundX = std::abs(part.cosR) * part.halfX +
                                         std::abs(part.sinR) * part.halfY;
                    const float boundY = std::abs(part.sinR) * part.halfX +
                                         std::abs(part.cosR) * part.halfY;
                    part.minX = part.cx - boundX; part.maxX = part.cx + boundX;
                    part.minY = part.cy - boundY; part.maxY = part.cy + boundY;
                    part.trigger = edge->isTrigger; part.layer = edge->layer;
                    colliders.push_back(part);
                }
            } else if (auto* polygon = entity->GetComponent<
                           SceneEditor::PolygonColliderComponent>()) {
                if (polygon->vertices.size() < 3) continue;
                const float radians = tr->rotation *
                    3.14159265358979323846f / 180.0f;
                const float cosR = std::cos(radians), sinR = std::sin(radians);
                c.kind = Collider::Kind::Polygon;
                c.minX = c.minY = 1e30f;
                c.maxX = c.maxY = -1e30f;
                c.cx = c.cy = 0.0f;
                for (const NaraGame::Vec2& point : polygon->vertices) {
                    const float x = (point.x + polygon->offset.x) * tr->scale.x;
                    const float y = (point.y + polygon->offset.y) * tr->scale.y;
                    NaraGame::Vec2 world{
                        tr->position.x + cosR * x - sinR * y,
                        tr->position.y + sinR * x + cosR * y};
                    c.vertices.push_back(world);
                    c.cx += world.x; c.cy += world.y;
                    c.minX = std::min(c.minX, world.x);
                    c.maxX = std::max(c.maxX, world.x);
                    c.minY = std::min(c.minY, world.y);
                    c.maxY = std::max(c.maxY, world.y);
                }
                c.cx /= float(c.vertices.size());
                c.cy /= float(c.vertices.size());
                c.trigger = polygon->isTrigger; c.layer = polygon->layer;
                colliders.push_back(c);
            } else if (auto* tilemap = entity->GetComponent<
                           SceneEditor::TilemapComponent>()) {
                SceneEditor::TilesetAsset* tileset =
                    m_assets.GetTileset(tilemap->tileset);
                if (tileset == nullptr || tileset->cellPixelW <= 0 ||
                    tileset->cellPixelH <= 0) continue;
                SceneEditor::TextureImportSettings settings;
                if (const auto* textureEntry =
                        m_assets.FindByGuid(tileset->texture)) {
                    settings.FromJson(textureEntry->importSettings);
                }
                const float cellW = float(tileset->cellPixelW) /
                    settings.pixelsPerUnit * std::abs(tr->scale.x);
                const float cellH = float(tileset->cellPixelH) /
                    settings.pixelsPerUnit * std::abs(tr->scale.y);
                const float radians = tr->rotation *
                    3.14159265358979323846f / 180.0f;
                const float cosR = std::cos(radians), sinR = std::sin(radians);
                for (const auto& [grid, tileIndex] : tilemap->cells) {
                    auto metadata = tileset->tiles.find(tileIndex);
                    if (metadata == tileset->tiles.end() ||
                        !metadata->second.collides) continue;
                    Collider cell = c;
                    cell.halfX = cellW * 0.5f; cell.halfY = cellH * 0.5f;
                    cell.cosR = cosR; cell.sinR = sinR;
                    const float localX = (float(grid.first) + 0.5f) * cellW;
                    const float localY = (float(grid.second) + 0.5f) * cellH;
                    cell.cx = tr->position.x + cosR * localX - sinR * localY;
                    cell.cy = tr->position.y + sinR * localX + cosR * localY;
                    const float boundX = std::abs(cosR) * cell.halfX +
                                         std::abs(sinR) * cell.halfY;
                    const float boundY = std::abs(sinR) * cell.halfX +
                                         std::abs(cosR) * cell.halfY;
                    cell.minX = cell.cx - boundX; cell.maxX = cell.cx + boundX;
                    cell.minY = cell.cy - boundY; cell.maxY = cell.cy + boundY;
                    cell.trigger = metadata->second.isTrigger;
                    cell.oneWay = metadata->second.shape ==
                        SceneEditor::TileColliderShape::OneWay;
                    cell.layer = tilemap->collisionLayer;
                    const auto shape = metadata->second.shape;
                    if (shape != SceneEditor::TileColliderShape::Box &&
                        shape != SceneEditor::TileColliderShape::OneWay) {
                        using Shape = SceneEditor::TileColliderShape;
                        std::vector<NaraGame::Vec2> normalized;
                        if (shape == Shape::SlopeRight)
                            normalized = {{0,0},{1,0},{1,1}};
                        else if (shape == Shape::SlopeLeft)
                            normalized = {{0,0},{1,0},{0,1}};
                        else if (shape == Shape::SlopeRightHalf)
                            normalized = {{0,0},{1,0},{1,0.5f}};
                        else if (shape == Shape::SlopeLeftHalf)
                            normalized = {{0,0},{1,0},{0,0.5f}};
                        else if (shape == Shape::SlopeRightHigh)
                            normalized = {{0,0},{1,0},{1,1},{0,0.5f}};
                        else if (shape == Shape::SlopeLeftHigh)
                            normalized = {{0,0},{1,0},{1,0.5f},{0,1}};
                        cell.kind = Collider::Kind::Polygon;
                        cell.vertices.clear(); cell.cx = cell.cy = 0.0f;
                        cell.minX = cell.minY = 1e30f;
                        cell.maxX = cell.maxY = -1e30f;
                        const float originX = float(grid.first) * cellW;
                        const float originY = float(grid.second) * cellH;
                        for (const auto& point : normalized) {
                            const float lx = originX + point.x * cellW;
                            const float ly = originY + point.y * cellH;
                            NaraGame::Vec2 world{
                                tr->position.x + cosR * lx - sinR * ly,
                                tr->position.y + sinR * lx + cosR * ly};
                            cell.vertices.push_back(world);
                            cell.cx += world.x; cell.cy += world.y;
                            cell.minX = std::min(cell.minX, world.x);
                            cell.maxX = std::max(cell.maxX, world.x);
                            cell.minY = std::min(cell.minY, world.y);
                            cell.maxY = std::max(cell.maxY, world.y);
                        }
                        if (!cell.vertices.empty()) {
                            cell.cx /= float(cell.vertices.size());
                            cell.cy /= float(cell.vertices.size());
                        }
                    } else {
                        cell.kind = Collider::Kind::Box;
                    }
                    colliders.push_back(std::move(cell));
                }
            }
        }
        std::set<std::pair<SceneEditor::EntityId, SceneEditor::EntityId>> now;
        std::map<std::pair<SceneEditor::EntityId, SceneEditor::EntityId>, bool> solid;
        for (size_t i = 0; i < colliders.size(); ++i) {
            for (size_t j = i + 1; j < colliders.size(); ++j) {
                Collider& a = colliders[i]; Collider& b = colliders[j];
                if (a.id == b.id) continue;
                if (!m_project.CollisionLayersCollide(a.layer, b.layer) ||
                    a.maxX <= b.minX || b.maxX <= a.minX ||
                    a.maxY <= b.minY || b.maxY <= a.minY) continue;
                float mtvX = 0.0f, mtvY = 0.0f;
                if (!ComputeMtv(a, b, mtvX, mtvY)) continue;
                const auto key = std::minmax(a.id, b.id);
                const bool isSolid = !a.trigger && !b.trigger;
                if (isSolid && (a.oneWay || b.oneWay)) {
                    const Collider& bodyCollider = a.oneWay ? b : a;
                    SceneEditor::Entity* bodyEntity = m_scene.Get(bodyCollider.id);
                    auto* body = bodyEntity ? bodyEntity->GetComponent<
                        SceneEditor::Rigidbody2DComponent>() : nullptr;
                    const float bodyPushY = a.oneWay ? -mtvY : mtvY;
                    if (bodyPushY <= 1e-4f ||
                        (body != nullptr && body->velocity.y > 1e-4f)) continue;
                }
                if (isSolid) ResolvePair(a, b, mtvX, mtvY);
                if (now.count(key)) continue;
                now.insert(key);
                solid[key] = isSolid;
                const bool existing = m_contacts.count(key) != 0;
                auto dispatch = [&](const Collider& self, const Collider& other) {
                    if (!self.scripted) return;
                    if (isSolid) {
                        if (existing) m_scripts.CallOnCollisionStay(
                            self.name, other.name, self.entry);
                        else m_scripts.CallOnCollisionEnter(
                            self.name, other.name, self.entry);
                    } else {
                        if (existing) m_scripts.CallOnTriggerStay(
                            self.name, other.name, self.entry);
                        else m_scripts.CallOnTriggerEnter(
                            self.name, other.name, self.entry);
                    }
                };
                dispatch(a, b); dispatch(b, a);
            }
        }
        for (const auto& key : m_contacts) {
            if (now.count(key)) continue;
            SceneEditor::Entity* a = m_scene.Get(key.first);
            SceneEditor::Entity* b = m_scene.Get(key.second);
            if (a == nullptr || b == nullptr) continue;
            const bool wasSolid = m_contactSolid[key];
            auto dispatch = [&](SceneEditor::Entity* self,
                                SceneEditor::Entity* other) {
                auto* script = self->GetComponent<SceneEditor::ScriptComponent>();
                if (script == nullptr) return;
                if (wasSolid) m_scripts.CallOnCollisionExit(
                    self->name, other->name, script->entryFunction);
                else m_scripts.CallOnTriggerExit(
                    self->name, other->name, script->entryFunction);
            };
            dispatch(a, b); dispatch(b, a);
        }
        m_contacts = std::move(now);
        m_contactSolid = std::move(solid);
    }

    bool ComputeMtv(const Collider& a, const Collider& b,
                    float& outX, float& outY) const {
        if (a.kind == Collider::Kind::Circle &&
            b.kind == Collider::Kind::Circle) {
            const float dx = a.cx - b.cx, dy = a.cy - b.cy;
            const float distanceSq = dx * dx + dy * dy;
            const float radii = a.radius + b.radius;
            if (distanceSq >= radii * radii) return false;
            const float distance = std::sqrt(distanceSq);
            if (distance > 1e-6f) {
                const float penetration = radii - distance;
                outX = dx / distance * penetration;
                outY = dy / distance * penetration;
            } else {
                outX = radii; outY = 0.0f;
            }
            return true;
        }
        auto shapeVertices = [](const Collider& shape) {
            if (shape.kind == Collider::Kind::Polygon) return shape.vertices;
            std::vector<NaraGame::Vec2> result;
            result.reserve(4);
            for (const NaraGame::Vec2 local : {
                    NaraGame::Vec2{-shape.halfX, -shape.halfY},
                    NaraGame::Vec2{ shape.halfX, -shape.halfY},
                    NaraGame::Vec2{ shape.halfX,  shape.halfY},
                    NaraGame::Vec2{-shape.halfX,  shape.halfY}}) {
                result.push_back({shape.cx + shape.cosR * local.x - shape.sinR * local.y,
                                  shape.cy + shape.sinR * local.x + shape.cosR * local.y});
            }
            return result;
        };
        if (a.kind != Collider::Kind::Circle &&
            b.kind != Collider::Kind::Circle) {
            const auto verticesA = shapeVertices(a);
            const auto verticesB = shapeVertices(b);
            float minimum = 1e30f, bestX = 0.0f, bestY = 0.0f;
            const float centerDx = a.cx - b.cx, centerDy = a.cy - b.cy;
            auto testAxes = [&](const std::vector<NaraGame::Vec2>& source) {
                for (size_t i = 0; i < source.size(); ++i) {
                    const auto& p0 = source[i];
                    const auto& p1 = source[(i + 1) % source.size()];
                    float ax = -(p1.y - p0.y), ay = p1.x - p0.x;
                    const float length = std::sqrt(ax * ax + ay * ay);
                    if (length <= 1e-6f) continue;
                    ax /= length; ay /= length;
                    auto project = [&](const std::vector<NaraGame::Vec2>& vertices,
                                       float& minimumValue, float& maximumValue) {
                        minimumValue = maximumValue = vertices[0].x * ax + vertices[0].y * ay;
                        for (size_t v = 1; v < vertices.size(); ++v) {
                            const float value = vertices[v].x * ax + vertices[v].y * ay;
                            minimumValue = std::min(minimumValue, value);
                            maximumValue = std::max(maximumValue, value);
                        }
                    };
                    float minA, maxA, minB, maxB;
                    project(verticesA, minA, maxA); project(verticesB, minB, maxB);
                    const float overlap = std::min(maxA, maxB) - std::max(minA, minB);
                    if (overlap <= 0.0f) return false;
                    if (overlap < minimum) {
                        minimum = overlap; bestX = ax; bestY = ay;
                        if (centerDx * ax + centerDy * ay < 0.0f) {
                            bestX = -bestX; bestY = -bestY;
                        }
                    }
                }
                return true;
            };
            if (!testAxes(verticesA) || !testAxes(verticesB)) return false;
            outX = bestX * minimum; outY = bestY * minimum;
            return true;
        }
        const Collider& circle = a.kind == Collider::Kind::Circle ? a : b;
        const Collider& other = a.kind == Collider::Kind::Circle ? b : a;
        if (other.kind == Collider::Kind::Polygon) {
            const auto& vertices = other.vertices;
            float bestDistanceSq = 1e30f, closestX = 0.0f, closestY = 0.0f;
            bool inside = true;
            float windingSign = 0.0f;
            for (size_t i = 0; i < vertices.size(); ++i) {
                const auto& p0 = vertices[i];
                const auto& p1 = vertices[(i + 1) % vertices.size()];
                const float ex = p1.x - p0.x, ey = p1.y - p0.y;
                const float cross = ex * (circle.cy - p0.y) - ey * (circle.cx - p0.x);
                if (std::abs(cross) > 1e-6f) {
                    if (windingSign == 0.0f) windingSign = cross;
                    else if ((cross > 0.0f) != (windingSign > 0.0f)) inside = false;
                }
                const float lengthSq = ex * ex + ey * ey;
                if (lengthSq <= 1e-10f) continue;
                const float t = std::clamp(
                    ((circle.cx - p0.x) * ex + (circle.cy - p0.y) * ey) /
                    lengthSq, 0.0f, 1.0f);
                const float px = p0.x + t * ex, py = p0.y + t * ey;
                const float dx = circle.cx - px, dy = circle.cy - py;
                const float distanceSq = dx * dx + dy * dy;
                if (distanceSq < bestDistanceSq) {
                    bestDistanceSq = distanceSq; closestX = px; closestY = py;
                }
            }
            if (!inside && bestDistanceSq >= circle.radius * circle.radius) return false;
            const float distance = std::sqrt(std::max(bestDistanceSq, 0.0f));
            float dx = circle.cx - closestX, dy = circle.cy - closestY;
            if (distance > 1e-6f) {
                const float penetration = inside ? circle.radius + distance
                                                 : circle.radius - distance;
                const float direction = inside ? -1.0f : 1.0f;
                dx = dx / distance * penetration * direction;
                dy = dy / distance * penetration * direction;
            } else {
                dx = circle.radius; dy = 0.0f;
            }
            if (a.kind == Collider::Kind::Circle) { outX = dx; outY = dy; }
            else { outX = -dx; outY = -dy; }
            return true;
        }
        const Collider& box = other;
        const float worldDx = circle.cx - box.cx;
        const float worldDy = circle.cy - box.cy;
        const float localX = box.cosR * worldDx + box.sinR * worldDy;
        const float localY = -box.sinR * worldDx + box.cosR * worldDy;
        const float closestX = std::clamp(localX, -box.halfX, box.halfX);
        const float closestY = std::clamp(localY, -box.halfY, box.halfY);
        float dx = localX - closestX, dy = localY - closestY;
        float distanceSq = dx * dx + dy * dy;
        if (distanceSq >= circle.radius * circle.radius) return false;
        if (distanceSq > 1e-10f) {
            const float distance = std::sqrt(distanceSq);
            const float penetration = circle.radius - distance;
            dx = dx / distance * penetration;
            dy = dy / distance * penetration;
        } else {
            const float left = localX + box.halfX;
            const float right = box.halfX - localX;
            const float bottom = localY + box.halfY;
            const float top = box.halfY - localY;
            const float nearest = std::min({left, right, bottom, top});
            if (nearest == left) dx = -(circle.radius + left);
            else if (nearest == right) dx = circle.radius + right;
            else if (nearest == bottom) dy = -(circle.radius + bottom);
            else dy = circle.radius + top;
        }
        const float rotatedX = box.cosR * dx - box.sinR * dy;
        const float rotatedY = box.sinR * dx + box.cosR * dy;
        if (a.kind == Collider::Kind::Circle) {
            outX = rotatedX; outY = rotatedY;
        } else {
            outX = -rotatedX; outY = -rotatedY;
        }
        return true;
    }

    void ResolvePair(const Collider& a, const Collider& b,
                     float px, float py) {
        SceneEditor::Entity* ea = m_scene.Get(a.id);
        SceneEditor::Entity* eb = m_scene.Get(b.id);
        auto* ta = ea ? ea->GetComponent<SceneEditor::TransformComponent>() : nullptr;
        auto* tb = eb ? eb->GetComponent<SceneEditor::TransformComponent>() : nullptr;
        auto* ra = ea ? ea->GetComponent<SceneEditor::Rigidbody2DComponent>() : nullptr;
        auto* rb = eb ? eb->GetComponent<SceneEditor::Rigidbody2DComponent>() : nullptr;
        const float invA = ra && ra->mass > 0 ? 1.0f / ra->mass : 0.0f;
        const float invB = rb && rb->mass > 0 ? 1.0f / rb->mass : 0.0f;
        const float sum = invA + invB;
        if (sum <= 0.0f) return;
        const float restitution = std::max(
            ra ? ra->restitution : 0.0f, rb ? rb->restitution : 0.0f);
        const float friction = std::max(
            ra ? ra->friction : 0.0f, rb ? rb->friction : 0.0f);
        const float magnitude = std::sqrt(px * px + py * py);
        auto move = [magnitude, restitution, friction](
                       SceneEditor::TransformComponent* tr,
                       SceneEditor::Rigidbody2DComponent* body,
                       float x, float y, float share) {
            if (tr == nullptr || share <= 0) return;
            tr->position.x += x * share; tr->position.y += y * share;
            if (body == nullptr || magnitude <= 1e-6f) return;
            const float nx = x / magnitude, ny = y / magnitude;
            const float normalVelocity = body->velocity.x * nx +
                                         body->velocity.y * ny;
            if (normalVelocity < 0.0f) {
                const float impulse = -(1.0f + restitution) * normalVelocity;
                body->velocity.x += impulse * nx;
                body->velocity.y += impulse * ny;
            }
            const float tx = -ny, ty = nx;
            const float tangentVelocity = body->velocity.x * tx +
                                          body->velocity.y * ty;
            body->velocity.x -= tangentVelocity * friction * tx;
            body->velocity.y -= tangentVelocity * friction * ty;
        };
        move(ta, ra, px, py, invA / sum);
        move(tb, rb, -px, -py, invB / sum);
    }

    void CollectIds(SceneEditor::EntityId id,
                    std::vector<SceneEditor::EntityId>& out) const {
        const SceneEditor::Entity* entity = m_scene.Get(id);
        if (entity == nullptr) return;
        out.push_back(id);
        for (SceneEditor::EntityId child : entity->children) CollectIds(child, out);
    }

    void NotifyScriptsForDestroy() {
        std::vector<SceneEditor::EntityId> ids;
        for (SceneEditor::EntityId root : m_scene.Roots()) CollectIds(root, ids);
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* entity = m_scene.Get(id);
            if (entity == nullptr) continue;
            auto* script = entity->GetComponent<SceneEditor::ScriptComponent>();
            if (script == nullptr || !script->started) continue;
            if (script->lastSeenEnabled)
                m_scripts.CallOnDisable(entity->name, script->entryFunction);
            m_scripts.CallOnDestroy(entity->name, script->entryFunction);
        }
    }

    void LoadPendingScene() {
        const std::string requested = m_scripts.ConsumePendingSceneLoad();
        if (requested.empty()) return;
        std::filesystem::path path(requested);
        if (!path.is_absolute()) {
            std::string rel = requested;
            if (rel.rfind("Assets/", 0) == 0) rel.erase(0, 7);
            path = std::filesystem::path(m_project.AssetsDir()) / rel;
        }
        NotifyScriptsForDestroy();
        if (!m_scene.Load(path.string())) {
            NARAGAME_ERROR("Player: scene.load failed for '%s'",
                           path.string().c_str());
        }
    }

    static NaraGame::Vec2 Rotate(NaraGame::Vec2 value, float degrees) {
        const float radians = degrees * 3.14159265358979323846f / 180.0f;
        const float c = std::cos(radians), s = std::sin(radians);
        return {value.x * c - value.y * s, value.x * s + value.y * c};
    }

    void Collect(SceneEditor::EntityId id, const WorldTransform& parent,
                 bool parentEnabled, std::vector<SpriteDraw>& draws,
                 NaraGame::Vec2& cameraPosition, float& pixelsPerUnit,
                 int& cameraPriority) {
        const SceneEditor::Entity* entity = m_scene.Get(id);
        if (entity == nullptr) return;
        const auto* transform = entity->GetComponent<
            SceneEditor::TransformComponent>();
        WorldTransform world = parent;
        if (transform != nullptr) {
            const NaraGame::Vec2 local{
                transform->position.x * parent.scale.x,
                transform->position.y * parent.scale.y};
            world.position = parent.position + Rotate(local, parent.rotation);
            world.scale = {parent.scale.x * transform->scale.x,
                           parent.scale.y * transform->scale.y};
            world.rotation = parent.rotation + transform->rotation;
        }
        const bool enabled = parentEnabled && entity->enabled;
        if (enabled) {
            if (const auto* camera = entity->GetComponent<
                    SceneEditor::CameraComponent>();
                camera != nullptr && camera->priority > cameraPriority) {
                cameraPriority = camera->priority;
                cameraPosition = world.position;
                pixelsPerUnit = std::max(1.0f, camera->zoom);
            }
            if (const auto* sprite = entity->GetComponent<
                    SceneEditor::SpriteRendererComponent>(); sprite != nullptr) {
                draws.push_back({entity, sprite, nullptr, world,
                    m_project.LayerPriority(sprite->sortingLayer)});
            }
            if (const auto* tilemap = entity->GetComponent<
                    SceneEditor::TilemapComponent>(); tilemap != nullptr) {
                draws.push_back({entity, nullptr, tilemap, world,
                    m_project.LayerPriority(tilemap->sortingLayer)});
            }
        }
        for (SceneEditor::EntityId child : entity->children) {
            Collect(child, world, enabled, draws, cameraPosition,
                    pixelsPerUnit, cameraPriority);
        }
    }

    void DrawSprite(const SpriteDraw& draw) {
        const auto* entry = m_assets.FindByGuid(draw.sprite->sprite);
        NaraGame::RHITexture* texture =
            m_assets.GetTexture(GetDevice(), draw.sprite->sprite);
        if (entry == nullptr || texture == nullptr) return;

        SceneEditor::TextureImportSettings settings;
        settings.FromJson(entry->importSettings);
        float sourceW = float(texture->Width());
        float sourceH = float(texture->Height());
        NaraGame::Vec4 uv{0.0f, 0.0f, 1.0f, 1.0f};
        NaraGame::Vec2 pivot = settings.ResolvedPivot();
        if (!draw.sprite->subSprite.empty()) {
            for (const SceneEditor::SubSprite& sub : settings.spriteSheet.sprites) {
                if (sub.name != draw.sprite->subSprite) continue;
                sourceW = float(sub.w); sourceH = float(sub.h);
                uv = {float(sub.x) / texture->Width(),
                      float(sub.y) / texture->Height(),
                      float(sub.x + sub.w) / texture->Width(),
                      float(sub.y + sub.h) / texture->Height()};
                pivot = sub.pivot;
                break;
            }
        }
        if (draw.sprite->flipX) std::swap(uv.x, uv.z);
        if (draw.sprite->flipY) std::swap(uv.y, uv.w);
        const NaraGame::Vec2 size{
            sourceW / settings.pixelsPerUnit * draw.world.scale.x,
            sourceH / settings.pixelsPerUnit * draw.world.scale.y};
        const NaraGame::Vec2 pivotOffset{
            (0.5f - pivot.x) * size.x,
            (0.5f - pivot.y) * size.y};
        const NaraGame::TextureRegion region(texture, uv,
                                             {sourceW, sourceH});
        m_renderer.DrawQuad(
            draw.world.position + Rotate(pivotOffset, draw.world.rotation),
            size, draw.world.rotation * 3.14159265358979323846f / 180.0f,
            region, draw.sprite->color);
    }

    void DrawTilemap(const SpriteDraw& draw) {
        SceneEditor::AssetGuid textureGuid = draw.tilemap->tileset;
        int cellW = draw.tilemap->cellPixelW;
        int cellH = draw.tilemap->cellPixelH;
        if (const auto* entry = m_assets.FindByGuid(draw.tilemap->tileset);
            entry != nullptr && entry->typeName == "Tileset") {
            if (SceneEditor::TilesetAsset* tileset =
                    m_assets.GetTileset(draw.tilemap->tileset)) {
                textureGuid = tileset->texture;
                cellW = tileset->cellPixelW;
                cellH = tileset->cellPixelH;
            }
        }
        if (cellW <= 0 || cellH <= 0) return;
        NaraGame::RHITexture* texture =
            m_assets.GetTexture(GetDevice(), textureGuid);
        if (texture == nullptr) return;
        const int cols = int(texture->Width()) / cellW;
        const int rows = int(texture->Height()) / cellH;
        if (cols <= 0 || rows <= 0) return;
        SceneEditor::TextureImportSettings settings;
        if (const auto* entry = m_assets.FindByGuid(textureGuid))
            settings.FromJson(entry->importSettings);
        const NaraGame::Vec2 cellSize{
            float(cellW) / settings.pixelsPerUnit * draw.world.scale.x,
            float(cellH) / settings.pixelsPerUnit * draw.world.scale.y};
        for (const auto& [grid, index] : draw.tilemap->cells) {
            if (index < 0 || index / cols >= rows) continue;
            const int col = index % cols, row = index / cols;
            const NaraGame::Vec4 uv{
                float(col * cellW) / texture->Width(),
                float(row * cellH) / texture->Height(),
                float((col + 1) * cellW) / texture->Width(),
                float((row + 1) * cellH) / texture->Height()};
            const NaraGame::Vec2 localCenter{
                (float(grid.first) + 0.5f) * cellSize.x,
                (float(grid.second) + 0.5f) * cellSize.y};
            const NaraGame::TextureRegion region(
                texture, uv, {float(cellW), float(cellH)});
            m_renderer.DrawQuad(
                draw.world.position + Rotate(localCenter, draw.world.rotation),
                cellSize,
                draw.world.rotation * 3.14159265358979323846f / 180.0f,
                region, NaraGame::Color::White());
        }
    }

    void TickAnimators(float dt) {
        std::vector<SceneEditor::EntityId> ids;
        for (SceneEditor::EntityId root : m_scene.Roots()) CollectIds(root, ids);
        for (SceneEditor::EntityId id : ids) {
            SceneEditor::Entity* entity = m_scene.Get(id);
            if (entity == nullptr || !entity->enabled) continue;
            auto* animator = entity->GetComponent<SceneEditor::AnimatorComponent>();
            auto* sprite = entity->GetComponent<
                SceneEditor::SpriteRendererComponent>();
            if (animator == nullptr || sprite == nullptr || !animator->isPlaying)
                continue;
            SceneEditor::AnimationAsset* animation = nullptr;
            float speed = 1.0f;
            bool transitionInProgress = false;
            if (animator->controller.IsValid()) {
                SceneEditor::AnimatorControllerAsset* controller =
                    m_assets.GetAnimatorController(animator->controller);
                if (controller == nullptr || controller->states.empty()) continue;
                if (animator->currentState < 0 ||
                    animator->currentState >= int(controller->states.size())) {
                    animator->currentState = std::clamp(
                        controller->defaultState, 0,
                        int(controller->states.size()) - 1);
                    animator->currentTime = animator->prevTime = 0.0f;
                    using PT = SceneEditor::AnimatorParameter::Type;
                    for (const auto& p : controller->parameters) {
                        if (p.type == PT::Float) animator->paramFloats.try_emplace(p.name, p.defaultFloat);
                        else if (p.type == PT::Int) animator->paramInts.try_emplace(p.name, p.defaultInt);
                        else if (p.type == PT::Vec2) animator->paramVec2.try_emplace(p.name, p.defaultVec2);
                        else if (p.type == PT::Color) animator->paramColors.try_emplace(p.name, p.defaultColor);
                        else if (p.type == PT::String) animator->paramStrings.try_emplace(p.name, p.defaultString);
                        else animator->paramBools.try_emplace(
                            p.name, p.type == PT::Trigger ? false : p.defaultBool);
                    }
                }
                transitionInProgress = TickAnimatorTransition(
                    *animator, *entity, dt);
                const auto& state = controller->states[size_t(animator->currentState)];
                animation = m_assets.GetAnimation(state.animation);
                speed = state.speed;
            } else {
                animation = m_assets.GetAnimation(animator->animation);
            }
            if (animation == nullptr || animation->frames.empty()) continue;
            const float previous = animator->currentTime;
            animator->currentTime += dt * speed;
            animator->prevTime = previous;
            if (const SceneEditor::AnimationFrame* frame =
                    animation->Sample(animator->currentTime)) {
                sprite->subSprite = frame->sprite;
            }
            const float total = animation->TotalDuration();
            for (const SceneEditor::AnimationEvent& event : animation->events) {
                bool crossed = event.time > previous &&
                               event.time <= animator->currentTime;
                if (animation->loop && total > 0.0f &&
                    int(previous / total) != int(animator->currentTime / total)) {
                    crossed = event.time <= std::fmod(animator->currentTime, total) ||
                              event.time > std::fmod(previous, total);
                }
                if (crossed) m_scripts.OnAnimationEvent(
                    entity->name, "Animation", event);
            }
            if (animator->controller.IsValid() && !transitionInProgress) {
                EvaluateAnimatorTransitions(*animator, *entity, *animation);
            }
        }
    }

    bool TickAnimatorTransition(SceneEditor::AnimatorComponent& animator,
                                SceneEditor::Entity& entity, float dt) {
        if (animator.transitionPending < 0 ||
            animator.transitionDuration <= 0.0f) return false;
        animator.transitionElapsed += dt;
        const float t = std::clamp(animator.transitionElapsed /
            animator.transitionDuration, 0.0f, 1.0f);
        if (auto* sprite = entity.GetComponent<
                SceneEditor::SpriteRendererComponent>()) {
            sprite->color.a = animator.transitionDesignAlpha *
                              std::abs(2.0f * t - 1.0f);
        }
        if (!animator.transitionSwapped && t >= 0.5f) {
            animator.currentState = animator.transitionPending;
            animator.currentTime = animator.prevTime = 0.0f;
            animator.transitionSwapped = true;
        }
        if (t < 1.0f) return true;
        if (auto* sprite = entity.GetComponent<
                SceneEditor::SpriteRendererComponent>()) {
            sprite->color.a = animator.transitionDesignAlpha;
        }
        if (!animator.transitionTriggerParam.empty()) {
            animator.paramBools[animator.transitionTriggerParam] = false;
        }
        animator.transitionPending = -1;
        animator.transitionDuration = animator.transitionElapsed = 0.0f;
        animator.transitionSwapped = false;
        animator.transitionTriggerParam.clear();
        return false;
    }

    void EvaluateAnimatorTransitions(
            SceneEditor::AnimatorComponent& animator,
            SceneEditor::Entity& entity,
            const SceneEditor::AnimationAsset& animation) {
        auto* controller = m_assets.GetAnimatorController(animator.controller);
        if (controller == nullptr) return;
        using PT = SceneEditor::AnimatorParameter::Type;
        using OP = SceneEditor::AnimatorTransition::Op;
        for (const auto& transition : controller->transitions) {
            if (transition.fromIdx >= 0 &&
                transition.fromIdx != animator.currentState) continue;
            if (transition.toIdx < 0 ||
                transition.toIdx >= int(controller->states.size())) continue;
            if (transition.hasExitTime) {
                const float total = animation.TotalDuration();
                if (total <= 0.0f || animator.currentTime <
                    transition.exitTime * total) continue;
            }
            if (transition.param.empty()) {
                if (!transition.hasExitTime) continue;
                if (transition.duration > 0.0f) {
                    animator.transitionPending = transition.toIdx;
                    animator.transitionDuration = transition.duration;
                    animator.transitionElapsed = 0.0f;
                    animator.transitionSwapped = false;
                    animator.transitionTriggerParam.clear();
                    if (auto* sprite = entity.GetComponent<
                            SceneEditor::SpriteRendererComponent>()) {
                        animator.transitionDesignAlpha = sprite->color.a;
                    }
                } else {
                    animator.currentState = transition.toIdx;
                    animator.currentTime = animator.prevTime = 0.0f;
                }
                break;
            }
            const SceneEditor::AnimatorParameter* parameter = nullptr;
            for (const auto& candidate : controller->parameters) {
                if (candidate.name == transition.param) {
                    parameter = &candidate; break;
                }
            }
            if (parameter == nullptr) continue;
            auto compare = [&](auto value, auto target) {
                switch (transition.op) {
                case OP::Equals: return value == target;
                case OP::NotEqual: return value != target;
                case OP::Less: return value < target;
                case OP::LessEqual: return value <= target;
                case OP::Greater: return value > target;
                case OP::GreaterEqual: return value >= target;
                }
                return false;
            };
            bool fire = false;
            if (parameter->type == PT::Float) {
                fire = compare(animator.paramFloats[parameter->name],
                               transition.valueFloat);
            } else if (parameter->type == PT::Int) {
                fire = compare(animator.paramInts[parameter->name],
                               transition.valueInt);
            } else if (parameter->type == PT::Vec2) {
                const auto value = animator.paramVec2[parameter->name];
                const bool equal = value.x == transition.valueVec2.x &&
                                   value.y == transition.valueVec2.y;
                fire = transition.op == OP::NotEqual ? !equal : equal;
            } else if (parameter->type == PT::Color) {
                const auto value = animator.paramColors[parameter->name];
                const auto target = transition.valueColor;
                const bool equal = value.r == target.r && value.g == target.g &&
                                   value.b == target.b && value.a == target.a;
                fire = transition.op == OP::NotEqual ? !equal : equal;
            } else if (parameter->type == PT::String) {
                const bool equal = animator.paramStrings[parameter->name] ==
                                   transition.valueString;
                fire = transition.op == OP::NotEqual ? !equal : equal;
            } else {
                const bool value = animator.paramBools[parameter->name];
                fire = transition.op == OP::NotEqual
                    ? value != transition.valueBool
                    : value == transition.valueBool;
            }
            if (!fire) continue;
            if (transition.duration > 0.0f) {
                animator.transitionPending = transition.toIdx;
                animator.transitionDuration = transition.duration;
                animator.transitionElapsed = 0.0f;
                animator.transitionSwapped = false;
                animator.transitionTriggerParam = parameter->type == PT::Trigger
                    ? parameter->name : std::string{};
                if (auto* sprite = entity.GetComponent<
                        SceneEditor::SpriteRendererComponent>()) {
                    animator.transitionDesignAlpha = sprite->color.a;
                }
            } else {
                animator.currentState = transition.toIdx;
                animator.currentTime = animator.prevTime = 0.0f;
                if (parameter->type == PT::Trigger)
                    animator.paramBools[parameter->name] = false;
            }
            break;
        }
    }

    std::string m_projectRoot;
    SceneEditor::Project m_project;
    SceneEditor::AssetDatabase m_assets;
    SceneEditor::Scene m_scene;
    NaraGame::Renderer2D m_renderer;
    SceneEditor::ScriptHost m_scripts;
    bool m_ready = false;
    int m_maxFrames = 0;
    int m_frames = 0;
    std::set<std::pair<SceneEditor::EntityId, SceneEditor::EntityId>> m_contacts;
    std::map<std::pair<SceneEditor::EntityId, SceneEditor::EntityId>, bool>
        m_contactSolid;
};

void PrepareProjectRoot(const std::string& projectRoot) {
    namespace fs = std::filesystem;
    const fs::path root(projectRoot);
    std::error_code ec;
    const fs::path pak = root / "assets.pak";
    if (fs::exists(pak, ec)) {
        SceneEditor::MountPak(pak.string());
        const fs::path assets = root / "Assets";
        const bool haveLooseAssets = fs::is_directory(assets, ec) &&
            fs::directory_iterator(assets, ec) != fs::directory_iterator();
        if (!haveLooseAssets && !SceneEditor::ExtractPak(
                pak.string(), assets.string())) {
            NARAGAME_ERROR("Player: failed to extract assets.pak");
        }
    }
}

NaraGamePlayer::LaunchOptions ParseArgs(int argc, char** argv) {
    NaraGamePlayer::LaunchOptions args;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--project") args.projectRoot = argv[++i];
        else if (key == "--frames") args.maxFrames = std::max(0, std::atoi(argv[++i]));
    }
    if (args.projectRoot.empty())
        args.projectRoot = std::filesystem::current_path().string();
    PrepareProjectRoot(args.projectRoot);
    return args;
}

} // namespace

NaraGamePlayer::LaunchOptions NaraGamePlayer::ParseLaunchOptions(
        int argc, char** argv) {
    return ParseArgs(argc, argv);
}

std::unique_ptr<NaraGame::Application> NaraGamePlayer::CreateProjectPlayer(
        LaunchOptions options) {
    PrepareProjectRoot(options.projectRoot);
    return std::make_unique<ProjectPlayer>(
        std::move(options.projectRoot), options.maxFrames);
}
