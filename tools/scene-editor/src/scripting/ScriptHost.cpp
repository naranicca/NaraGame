#include "ScriptHost.h"

#include "../asset/AnimatorController.h"
#include "../asset/AssetDatabase.h"
#include "../audio/Audio.h"
#include "../scene/AnimatorComponent.h"
#include "../scene/BoxColliderComponent.h"
#include "../scene/CircleColliderComponent.h"
#include "../scene/Component.h"
#include "../scene/CameraComponent.h"
#include "../scene/ParticleSystemComponent.h"
#include "../scene/Rigidbody2DComponent.h"
#include "../scene/Entity.h"
#include "../scene/Scene.h"
#include "../scene/ScriptComponent.h"
#include "../scene/SpriteRendererComponent.h"
#include "../scene/TilemapComponent.h"
#include "../scene/TransformComponent.h"
#include "../asset/Tileset.h"
#include "../asset/TextureImportSettings.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

#if defined(NARAGAME_SCENE_EDITOR_HAS_LUA) && NARAGAME_SCENE_EDITOR_HAS_LUA
#include <imgui.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(NARAGAME_SCENE_EDITOR_HAS_LUA) && NARAGAME_SCENE_EDITOR_HAS_LUA
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#endif

namespace SceneEditor {

void ScriptHost::AppendLine(const std::string& line) {
    m_outputLog.push_back(line);
    if (m_outputLog.size() > kOutputLogMax) {
        m_outputLog.erase(
            m_outputLog.begin(),
            m_outputLog.begin() +
                (m_outputLog.size() - kOutputLogMax));
    }
}

#if defined(NARAGAME_SCENE_EDITOR_HAS_LUA) && NARAGAME_SCENE_EDITOR_HAS_LUA
// -----------------------------------------------------------------
// Lua-backed implementation. The interpreter ships with a built-in
// default `OnAnimationEvent` handler so the editor reads the same
// "function call" lines whether or not the user has authored a
// custom script yet.
// -----------------------------------------------------------------

// Resolve the host pointer stashed as the closure upvalue. Used by
// every animator API callback to find its way back to the C++
// world.
static ScriptHost* HostFromUpvalue(lua_State* L) {
    return static_cast<ScriptHost*>(
        lua_touserdata(L, lua_upvalueindex(1)));
}

// Walks the bound scene + returns the AnimatorComponent attached
// to the named entity, or nullptr when either the entity or the
// component is missing. Scripts that drive a missing entity just
// silently no-op.
static AnimatorComponent* FindAnimator(ScriptHost* host,
                                       const char* entityName) {
    if (!host || !host->BoundScene() || !entityName) return nullptr;
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) return nullptr;
    return e->GetComponent<AnimatorComponent>();
}

// Same pattern for TransformComponent. Every entity carries one
// by convention, so a missing return value here usually means a
// stale entity name in the script.
static TransformComponent* FindTransform(ScriptHost* host,
                                         const char* entityName) {
    if (!host || !host->BoundScene() || !entityName) return nullptr;
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) return nullptr;
    return e->GetComponent<TransformComponent>();
}

static Rigidbody2DComponent* FindRigidbody(ScriptHost* host,
                                           const char* entityName) {
    if (!host || !host->BoundScene() || !entityName) return nullptr;
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) return nullptr;
    return e->GetComponent<Rigidbody2DComponent>();
}

// Map an editor-friendly key name to ImGui's enum. Supports the
// canonical 1-character A-Z / 0-9 spellings plus the most-used
// named keys (arrows, modifiers, common control keys). Anything
// the table doesn't know about returns ImGuiKey_None which the
// callbacks then treat as "key isn't down" -- no scripting error.
static ImGuiKey LookupImGuiKey(const char* name) {
    if (!name || !name[0]) return ImGuiKey_None;
    if (name[1] == '\0') {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z') return ImGuiKey(ImGuiKey_A + (c - 'A'));
        if (c >= '0' && c <= '9') return ImGuiKey(ImGuiKey_0 + (c - '0'));
    }
    struct Entry { const char* name; ImGuiKey key; };
    static const Entry kTable[] = {
        {"Space",      ImGuiKey_Space},
        {"Enter",      ImGuiKey_Enter},
        {"Return",     ImGuiKey_Enter},
        {"Escape",     ImGuiKey_Escape},
        {"Esc",        ImGuiKey_Escape},
        {"Tab",        ImGuiKey_Tab},
        {"Backspace",  ImGuiKey_Backspace},
        {"Delete",     ImGuiKey_Delete},
        {"Home",       ImGuiKey_Home},
        {"End",        ImGuiKey_End},
        {"PageUp",     ImGuiKey_PageUp},
        {"PageDown",   ImGuiKey_PageDown},
        {"Up",         ImGuiKey_UpArrow},
        {"UpArrow",    ImGuiKey_UpArrow},
        {"Down",       ImGuiKey_DownArrow},
        {"DownArrow",  ImGuiKey_DownArrow},
        {"Left",       ImGuiKey_LeftArrow},
        {"LeftArrow",  ImGuiKey_LeftArrow},
        {"Right",      ImGuiKey_RightArrow},
        {"RightArrow", ImGuiKey_RightArrow},
        {"LeftShift",  ImGuiKey_LeftShift},
        {"RightShift", ImGuiKey_RightShift},
        {"LeftCtrl",   ImGuiKey_LeftCtrl},
        {"RightCtrl",  ImGuiKey_RightCtrl},
        {"LeftAlt",    ImGuiKey_LeftAlt},
        {"RightAlt",   ImGuiKey_RightAlt},
    };
    for (const Entry& e : kTable) {
        if (std::strcmp(e.name, name) == 0) return e.key;
    }
    return ImGuiKey_None;
}

// input.is_key_down / is_key_pressed / is_key_released /
// mouse_position / is_mouse_down / is_mouse_pressed /
// is_mouse_released / mouse_wheel. Routes through ImGui's already-
// polled input state, so scripts see exactly what the editor's
// own panels see this frame -- no separate polling layer to keep
// in sync.
static int LuaInputIsKeyDown(lua_State* L) {
    ImGuiKey k = LookupImGuiKey(luaL_checkstring(L, 1));
    lua_pushboolean(L,
        k != ImGuiKey_None && ImGui::IsKeyDown(k));
    return 1;
}
static int LuaInputIsKeyPressed(lua_State* L) {
    ImGuiKey k = LookupImGuiKey(luaL_checkstring(L, 1));
    // Second arg controls key-repeat firing; defaults to false so
    // the common "fire once per press" pattern just works.
    bool repeat = false;
    if (lua_gettop(L) >= 2) repeat = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L,
        k != ImGuiKey_None && ImGui::IsKeyPressed(k, repeat));
    return 1;
}
static int LuaInputIsKeyReleased(lua_State* L) {
    ImGuiKey k = LookupImGuiKey(luaL_checkstring(L, 1));
    lua_pushboolean(L,
        k != ImGuiKey_None && ImGui::IsKeyReleased(k));
    return 1;
}
static int LuaInputMousePosition(lua_State* L) {
    const ImVec2 p = ImGui::GetMousePos();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}
static int LuaInputIsMouseDown(lua_State* L) {
    int btn = int(luaL_optinteger(L, 1, 0));
    lua_pushboolean(L,
        btn >= 0 && btn < 5 &&
        ImGui::IsMouseDown(ImGuiMouseButton(btn)));
    return 1;
}
static int LuaInputIsMousePressed(lua_State* L) {
    int btn = int(luaL_optinteger(L, 1, 0));
    lua_pushboolean(L,
        btn >= 0 && btn < 5 &&
        ImGui::IsMouseClicked(ImGuiMouseButton(btn)));
    return 1;
}
static int LuaInputIsMouseReleased(lua_State* L) {
    int btn = int(luaL_optinteger(L, 1, 0));
    lua_pushboolean(L,
        btn >= 0 && btn < 5 &&
        ImGui::IsMouseReleased(ImGuiMouseButton(btn)));
    return 1;
}
static int LuaInputMouseWheel(lua_State* L) {
    lua_pushnumber(L, ImGui::GetIO().MouseWheel);
    return 1;
}

// Walks an entity's tree firing CallOnDestroy on every started
// ScriptComponent we encounter -- recursing children before the
// parent so scripts see the same teardown order Scene's destroy
// path itself uses. Used by the Lua `scene.destroy_entity` path
// so a runtime-spawned entity tree shuts down cleanly.
static void FireScriptDestroyTree(ScriptHost* host, Scene* scene,
                                  EntityId id) {
    if (!host || !scene) return;
    Entity* e = scene->Get(id);
    if (!e) return;
    for (EntityId childId : e->children) {
        FireScriptDestroyTree(host, scene, childId);
    }
    if (auto* sc = e->GetComponent<ScriptComponent>()) {
        // OnDisable runs first when the entity was alive +
        // enabled -- mirrors the editor-side
        // NotifyScriptDestroyForEntity ordering.
        if (sc->lastSeenEnabled) {
            host->CallOnDisable(e->name, sc->entryFunction);
            sc->lastSeenEnabled = false;
        }
        if (sc->started) {
            host->CallOnDestroy(e->name, sc->entryFunction);
            sc->started = false;
        }
    }
}

// scene.create_entity / destroy_entity / find / entity_count /
// all_entities. Lets scripts spawn + tear down entities at
// runtime; every callback respects the bound Scene's invariants
// (auto-added TransformComponent, parent / child wiring,
// OnDestroy ordering).
static int LuaSceneCreateEntity(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) return 0;
    const char* name       = luaL_optstring(L, 1, "Entity");
    const char* parentName = luaL_optstring(L, 2, nullptr);
    Scene* scene = host->BoundScene();
    // Optional second arg names the parent. An empty / unknown
    // parent name silently falls through to creating a root --
    // the call still succeeds so a script that doesn't care
    // about hierarchy keeps reading like `scene.create_entity
    // "Foo"`.
    EntityId parentId = kInvalidEntityId;
    if (parentName && parentName[0] != '\0') {
        Entity* p = scene->FindByName(parentName);
        if (p) parentId = p->id;
    }
    EntityId id = scene->CreateEntity(name, parentId);
    Entity* e = scene->Get(id);
    lua_pushstring(L, e ? e->name.c_str() : "");
    return 1;
}
static int LuaSceneDestroyEntity(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushboolean(L, 0); return 1;
    }
    Scene* scene = host->BoundScene();
    Entity* e = scene->FindByName(luaL_checkstring(L, 1));
    if (!e) { lua_pushboolean(L, 0); return 1; }
    const EntityId id = e->id;
    FireScriptDestroyTree(host, scene, id);
    scene->DestroyEntity(id);
    lua_pushboolean(L, 1);
    return 1;
}
static int LuaSceneFind(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    Scene* scene = host ? host->BoundScene() : nullptr;
    lua_pushboolean(L,
        scene && scene->FindByName(luaL_checkstring(L, 1)) != nullptr);
    return 1;
}
static int LuaSceneEntityCount(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    Scene* scene = host ? host->BoundScene() : nullptr;
    int count = 0;
    if (scene) {
        // Walk the root list + recurse; mirrors what
        // CollectAllEntities does in EditorApp without pulling
        // that helper into the host.
        std::vector<EntityId> stack(scene->Roots().begin(),
                                    scene->Roots().end());
        while (!stack.empty()) {
            EntityId id = stack.back(); stack.pop_back();
            Entity* e = scene->Get(id);
            if (!e) continue;
            ++count;
            for (EntityId childId : e->children) stack.push_back(childId);
        }
    }
    lua_pushinteger(L, count);
    return 1;
}
// scene.broadcast(method, ...) -> int. Calls the matching
// lifecycle-style handler on every enabled entity that carries
// a ScriptComponent. Method-name resolution mirrors the M6c5
// rule: try `<entryFunction>_<method>` first, fall back to the
// bare global `On<method>`. Receivers get `(entityName, ...)`
// with the broadcast's caller-supplied trailing args verbatim.
// Returns the number of dispatches that completed without a
// Lua error.
static int LuaSceneBroadcast(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushinteger(L, 0); return 1;
    }
    const char* method = luaL_checkstring(L, 1);
    const int numArgs  = lua_gettop(L) - 1;
    const int argBase  = 2;  // Lua positions argBase..argBase+numArgs-1
    Scene* scene = host->BoundScene();
    int dispatched = 0;
    std::vector<EntityId> stack(scene->Roots().begin(),
                                scene->Roots().end());
    while (!stack.empty()) {
        EntityId id = stack.back(); stack.pop_back();
        Entity* e = scene->Get(id);
        if (!e) continue;
        for (EntityId childId : e->children) stack.push_back(childId);
        if (!e->enabled) continue;
        auto* sc = e->GetComponent<ScriptComponent>();
        if (!sc) continue;
        // Resolve the target function name through the same
        // entry-prefixed-then-bare-global rule as
        // PushLifecycleHook in CallOn*.
        std::string fn;
        if (!sc->entryFunction.empty()) {
            const std::string entryName =
                sc->entryFunction + "_" + method;
            lua_getglobal(L, entryName.c_str());
            if (lua_isfunction(L, -1)) {
                fn = entryName;
            } else {
                lua_pop(L, 1);
            }
        }
        if (fn.empty()) {
            const std::string bare = std::string("On") + method;
            lua_getglobal(L, bare.c_str());
            if (!lua_isfunction(L, -1)) {
                lua_pop(L, 1);
                continue;
            }
            fn = bare;
        }
        // Push the receiver's entity name + replay the trailing
        // args from the original call. lua_pushvalue copies the
        // stack slot so each loop iteration gets a fresh
        // duplicate.
        lua_pushstring(L, e->name.c_str());
        for (int i = 0; i < numArgs; ++i) {
            lua_pushvalue(L, argBase + i);
        }
        // Track the dispatching entity so a callee that calls
        // scene.destroy_self() targets the right one.
        host->SetDispatchEntity(e->name);
        if (lua_pcall(L, 1 + numArgs, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            host->AppendLine("[lua-error] " + fn + ": " +
                             (err ? err : "<no message>"));
            lua_pop(L, 1);
        } else {
            ++dispatched;
        }
        host->SetDispatchEntity("");
    }
    lua_pushinteger(L, dispatched);
    return 1;
}

// Segment-vs-AABB slab method. Returns true + sets `tHit` in
// [0, 1] when the segment from (ox, oy) along (dx, dy) (with
// unit-length t spanning the segment endpoints) crosses the
// box. tHit = 0 means the origin already starts inside; we
// keep that so a ray fired from inside still reports a hit
// at distance 0.
static bool RaySegmentVsBox(float ox, float oy, float dx, float dy,
                            float minX, float minY,
                            float maxX, float maxY,
                            float& tHit) {
    float tmin = 0.0f;
    float tmax = 1.0f;
    auto Slab = [&](float origin, float dir,
                    float lo, float hi) -> bool {
        if (std::fabs(dir) < 1e-6f) {
            return origin >= lo && origin <= hi;
        }
        const float invD = 1.0f / dir;
        float t1 = (lo - origin) * invD;
        float t2 = (hi - origin) * invD;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        return tmin <= tmax;
    };
    if (!Slab(ox, dx, minX, maxX)) return false;
    if (!Slab(oy, dy, minY, maxY)) return false;
    tHit = tmin;
    return true;
}

// M7c15: Segment-vs-OBB. Transforms the ray into the box's
// local frame (translate by -centre, rotate by -theta) so the
// existing slab test works as if the box were axis-aligned.
// tHit is parameter-space along the original segment, which is
// invariant under that rigid transform, so the value returned
// is correct in world space too.
static bool RaySegmentVsObb(float ox, float oy, float dx, float dy,
                            float bcx, float bcy,
                            float halfX, float halfY,
                            float cosR, float sinR,
                            float& tHit) {
    const float relOx = ox - bcx;
    const float relOy = oy - bcy;
    const float localOx =  cosR * relOx + sinR * relOy;
    const float localOy = -sinR * relOx + cosR * relOy;
    const float localDx =  cosR * dx + sinR * dy;
    const float localDy = -sinR * dx + cosR * dy;
    return RaySegmentVsBox(localOx, localOy, localDx, localDy,
                           -halfX, -halfY, halfX, halfY, tHit);
}

// M7c15: SAT for an axis-aligned query box against a rotated
// target box. Four candidate axes (query X / Y + target's
// rotated X / Y); any separating axis means no overlap.
static bool AabbVsObb(float qMinX, float qMaxX,
                      float qMinY, float qMaxY,
                      float bcx, float bcy,
                      float halfX, float halfY,
                      float cosR, float sinR) {
    const float qcx = (qMinX + qMaxX) * 0.5f;
    const float qcy = (qMinY + qMaxY) * 0.5f;
    const float qhx = (qMaxX - qMinX) * 0.5f;
    const float qhy = (qMaxY - qMinY) * 0.5f;
    auto Project = [](float cx, float cy,
                      float hX, float hY,
                      float xAxX, float xAxY,
                      float yAxX, float yAxY,
                      float ax, float ay,
                      float& outMin, float& outMax) {
        const float centerProj = cx * ax + cy * ay;
        const float xExt = hX * std::fabs(xAxX * ax + xAxY * ay);
        const float yExt = hY * std::fabs(yAxX * ax + yAxY * ay);
        const float ext = xExt + yExt;
        outMin = centerProj - ext;
        outMax = centerProj + ext;
    };
    const float axes[4][2] = {
        { 1.0f, 0.0f },           // query X
        { 0.0f, 1.0f },           // query Y
        {  cosR,  sinR },         // target X
        { -sinR,  cosR },         // target Y
    };
    for (int i = 0; i < 4; ++i) {
        float qMin, qMax, bMin, bMax;
        Project(qcx, qcy, qhx, qhy,
                1.0f, 0.0f, 0.0f, 1.0f,
                axes[i][0], axes[i][1], qMin, qMax);
        Project(bcx, bcy, halfX, halfY,
                cosR, sinR, -sinR, cosR,
                axes[i][0], axes[i][1], bMin, bMax);
        if (qMax < bMin || bMax < qMin) return false;
    }
    return true;
}

// Segment-vs-circle. Standard quadratic; returns the nearest
// non-negative root inside [0, 1]. Inside-the-circle origin
// reports tHit = 0 the same way the box variant does.
static bool RaySegmentVsCircle(float ox, float oy, float dx, float dy,
                               float cx, float cy, float r,
                               float& tHit) {
    const float fx = ox - cx;
    const float fy = oy - cy;
    if (fx * fx + fy * fy <= r * r) { tHit = 0.0f; return true; }
    const float a = dx * dx + dy * dy;
    const float b = 2.0f * (fx * dx + fy * dy);
    const float c = fx * fx + fy * fy - r * r;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;
    const float s = std::sqrt(disc);
    const float t1 = (-b - s) / (2.0f * a);
    const float t2 = (-b + s) / (2.0f * a);
    if (t1 >= 0.0f && t1 <= 1.0f) { tHit = t1; return true; }
    if (t2 >= 0.0f && t2 <= 1.0f) { tHit = t2; return true; }
    return false;
}

// scene.raycast(x0, y0, x1, y1) -> (name, hx, hy, distance).
// Walks every enabled entity that carries a Box or Circle
// collider, returns the nearest hit's entity name + hit point
// + distance from the origin. Misses return just "" so callers
// can write `if scene.raycast(...) ~= "" then`. Treats the
// 4-arg form as a finite segment from (x0, y0) to (x1, y1) --
// for unbounded rays the caller can scale (x1, y1) far away
// from the origin.
static int LuaSceneRaycast(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushstring(L, ""); return 1;
    }
    const float x0 = float(luaL_checknumber(L, 1));
    const float y0 = float(luaL_checknumber(L, 2));
    const float x1 = float(luaL_checknumber(L, 3));
    const float y1 = float(luaL_checknumber(L, 4));
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    Scene* scene = host->BoundScene();
    const Entity* bestE = nullptr;
    float bestT = 2.0f;  // any real hit has tHit <= 1
    std::vector<EntityId> stack(scene->Roots().begin(),
                                scene->Roots().end());
    while (!stack.empty()) {
        EntityId id = stack.back(); stack.pop_back();
        Entity* e = scene->Get(id);
        if (!e) continue;
        for (EntityId childId : e->children) stack.push_back(childId);
        if (!e->enabled) continue;
        auto* tr = e->GetComponent<TransformComponent>();
        if (!tr) continue;
        auto* bc = e->GetComponent<BoxColliderComponent>();
        auto* cc = e->GetComponent<CircleColliderComponent>();
        if (!bc && !cc) continue;
        float tHit = 0.0f;
        bool hit = false;
        if (bc) {
            const float halfX = bc->size.x * 0.5f * tr->scale.x;
            const float halfY = bc->size.y * 0.5f * tr->scale.y;
            const float cx    = tr->position.x + bc->offset.x;
            const float cy    = tr->position.y + bc->offset.y;
            const float rotRad = tr->rotation
                * 3.14159265358979323846f / 180.0f;
            const float cosR = std::cos(rotRad);
            const float sinR = std::sin(rotRad);
            hit = RaySegmentVsObb(x0, y0, dx, dy,
                                  cx, cy, halfX, halfY,
                                  cosR, sinR, tHit);
        } else {
            const float scaleAvg =
                (tr->scale.x + tr->scale.y) * 0.5f;
            const float cx = tr->position.x + cc->offset.x;
            const float cy = tr->position.y + cc->offset.y;
            const float r  = cc->radius * scaleAvg;
            hit = RaySegmentVsCircle(x0, y0, dx, dy, cx, cy, r, tHit);
        }
        if (hit && tHit < bestT) {
            bestT = tHit;
            bestE = e;
        }
    }
    if (!bestE) { lua_pushstring(L, ""); return 1; }
    const float hx   = x0 + dx * bestT;
    const float hy   = y0 + dy * bestT;
    const float dist = std::sqrt(dx * dx + dy * dy) * bestT;
    lua_pushstring(L, bestE->name.c_str());
    lua_pushnumber(L, hx);
    lua_pushnumber(L, hy);
    lua_pushnumber(L, dist);
    return 4;
}

// scene.overlap_box(cx, cy, w, h) -> table of entity names.
// Tests a virtual world-space AABB centred at (cx, cy) with
// full size (w, h) against every enabled entity that carries a
// Box or Circle collider. Useful for area-of-effect queries
// (explosion, AoE buff, trigger sensor) that don't need to be
// attached to a permanent entity.
static int LuaSceneOverlapBox(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    lua_newtable(L);
    if (!host || !host->BoundScene()) return 1;
    const float cx = float(luaL_checknumber(L, 1));
    const float cy = float(luaL_checknumber(L, 2));
    const float w  = float(luaL_checknumber(L, 3));
    const float h  = float(luaL_checknumber(L, 4));
    const float minX = cx - w * 0.5f;
    const float maxX = cx + w * 0.5f;
    const float minY = cy - h * 0.5f;
    const float maxY = cy + h * 0.5f;
    Scene* scene = host->BoundScene();
    int idx = 1;
    std::vector<EntityId> stack(scene->Roots().begin(),
                                scene->Roots().end());
    while (!stack.empty()) {
        EntityId id = stack.back(); stack.pop_back();
        Entity* e = scene->Get(id);
        if (!e) continue;
        for (EntityId childId : e->children) stack.push_back(childId);
        if (!e->enabled) continue;
        auto* tr = e->GetComponent<TransformComponent>();
        if (!tr) continue;
        auto* bc = e->GetComponent<BoxColliderComponent>();
        auto* cc = e->GetComponent<CircleColliderComponent>();
        if (!bc && !cc) continue;
        bool hit = false;
        if (bc) {
            const float halfX = bc->size.x * 0.5f * tr->scale.x;
            const float halfY = bc->size.y * 0.5f * tr->scale.y;
            const float ex = tr->position.x + bc->offset.x;
            const float ey = tr->position.y + bc->offset.y;
            const float rotRad = tr->rotation
                * 3.14159265358979323846f / 180.0f;
            const float cosR = std::cos(rotRad);
            const float sinR = std::sin(rotRad);
            hit = AabbVsObb(minX, maxX, minY, maxY,
                            ex, ey, halfX, halfY, cosR, sinR);
        } else {
            const float scaleAvg =
                (tr->scale.x + tr->scale.y) * 0.5f;
            const float ex = tr->position.x + cc->offset.x;
            const float ey = tr->position.y + cc->offset.y;
            const float r  = cc->radius * scaleAvg;
            const float qx = std::clamp(ex, minX, maxX);
            const float qy = std::clamp(ey, minY, maxY);
            const float ddx = ex - qx;
            const float ddy = ey - qy;
            hit = (ddx * ddx + ddy * ddy) <= r * r;
        }
        if (hit) {
            lua_pushstring(L, e->name.c_str());
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

// scene.find_path(sx, sy, ex, ey [, smooth]) -> table of
// {x, y} world points (or nil on failure). M7c29: 8-directional
// A* with octile heuristic running over the first Tilemap-
// with-Tileset the scene contains. Solid Box tiles
// (collides=true, isTrigger=false, shape=Box) block;
// everything else -- empty cells, triggers, OneWay, slopes --
// is walkable. Path includes the start cell, the goal cell,
// and every step in between (cell-centre world coords).
//
// M7c30: `smooth` (default true) post-processes the raw A*
// cell-path with a line-of-sight "string pulling" pass --
// from each kept waypoint we scan forward for the farthest
// later waypoint reachable by a straight line that touches
// only walkable cells, then drop everything between. Cuts
// the average waypoint count by 3-4x for open levels +
// matches what a hand-authored patrol route would look like.
// Pass `smooth=false` to get the raw zig-zag cell path for
// debugging.
//
// Cell coords use the tilemap's local frame -- a rotated
// tilemap's pathfinder still talks in unrotated cell indices,
// which is correct for grid-based movement; callers walking
// rotated terrain should re-rotate waypoints themselves.
//
// Bounded at 50k node expansions -- enough for a 100x100 fully-
// open grid; pathological mazes that exhaust it return nil so a
// Lua loop never freezes the editor.
static int LuaSceneFindPath(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    Scene* scene = host ? host->BoundScene() : nullptr;
    AssetDatabase* db = host ? host->BoundAssetDatabase() : nullptr;
    if (!scene || !db) { lua_pushnil(L); return 1; }
    const float sx = float(luaL_checknumber(L, 1));
    const float sy = float(luaL_checknumber(L, 2));
    const float ex = float(luaL_checknumber(L, 3));
    const float ey = float(luaL_checknumber(L, 4));
    // M7c30: smooth defaults to true so callers get the
    // post-processed path without naming the option; pass
    // false to inspect the raw A* cell path.
    bool smooth = true;
    if (lua_gettop(L) >= 5 && !lua_isnoneornil(L, 5)) {
        smooth = lua_toboolean(L, 5) != 0;
    }
    TilemapComponent* tm = nullptr;
    TransformComponent* tmTr = nullptr;
    TilesetAsset* ts = nullptr;
    float cellW = 1.0f, cellH = 1.0f;
    {
        std::vector<EntityId> stack(scene->Roots().begin(),
                                     scene->Roots().end());
        while (!stack.empty()) {
            EntityId id = stack.back(); stack.pop_back();
            Entity* e = scene->Get(id);
            if (!e) continue;
            for (EntityId childId : e->children) stack.push_back(childId);
            if (!e->enabled) continue;
            auto* trC = e->GetComponent<TransformComponent>();
            auto* tmC = e->GetComponent<TilemapComponent>();
            if (!trC || !tmC) continue;
            TilesetAsset* tsC = db->GetTileset(tmC->tileset);
            if (!tsC) continue;
            tm = tmC;
            tmTr = trC;
            ts = tsC;
            if (const auto* texEntry = db->FindByGuid(ts->texture)) {
                TextureImportSettings imp;
                imp.FromJson(texEntry->importSettings);
                cellW = float(ts->cellPixelW) / imp.pixelsPerUnit;
                cellH = float(ts->cellPixelH) / imp.pixelsPerUnit;
            }
            break;
        }
    }
    if (!tm || !ts || cellW < 1e-4f || cellH < 1e-4f) {
        lua_pushnil(L); return 1;
    }
    auto WorldToCell = [&](float wx, float wy,
                            int& gx, int& gy) {
        const float lx = (wx - tmTr->position.x) / cellW;
        const float ly = (wy - tmTr->position.y) / cellH;
        gx = int(std::floor(lx));
        gy = int(std::floor(ly));
    };
    auto IsWalkable = [&](int gx, int gy) -> bool {
        auto it = tm->cells.find({gx, gy});
        if (it == tm->cells.end()) return true;
        auto tIt = ts->tiles.find(it->second);
        if (tIt == ts->tiles.end()) return true;
        if (!tIt->second.collides) return true;
        if (tIt->second.isTrigger) return true;
        if (tIt->second.shape == TileColliderShape::Box) return false;
        return true;
    };
    int sgx, sgy, egx, egy;
    WorldToCell(sx, sy, sgx, sgy);
    WorldToCell(ex, ey, egx, egy);
    if (!IsWalkable(sgx, sgy) || !IsWalkable(egx, egy)) {
        lua_pushnil(L); return 1;
    }
    struct Node {
        int x, y;
        float g, f;
    };
    struct NodeCmp {
        bool operator()(const Node& a, const Node& b) const {
            return a.f > b.f;
        }
    };
    auto Key = [](int x, int y) -> int64_t {
        return (int64_t(x) << 32) |
               int64_t(uint32_t(y));
    };
    auto H = [&](int x, int y) -> float {
        const float dx = float(std::abs(x - egx));
        const float dy = float(std::abs(y - egy));
        return std::max(dx, dy) +
               (std::sqrt(2.0f) - 1.0f) * std::min(dx, dy);
    };
    std::priority_queue<Node, std::vector<Node>, NodeCmp> open;
    std::unordered_map<int64_t,
        std::pair<int, int>> cameFrom;
    std::unordered_map<int64_t, float> gScore;
    gScore[Key(sgx, sgy)] = 0.0f;
    open.push({sgx, sgy, 0.0f, H(sgx, sgy)});
    static const int kDx[8] = { 1, -1, 0, 0,  1,  1, -1, -1 };
    static const int kDy[8] = { 0, 0, 1, -1,  1, -1,  1, -1 };
    int iter = 0;
    const int kMaxIter = 50000;
    bool found = false;
    while (!open.empty() && iter++ < kMaxIter) {
        Node curr = open.top(); open.pop();
        const int64_t cKey = Key(curr.x, curr.y);
        auto gIt = gScore.find(cKey);
        if (gIt != gScore.end() && curr.g > gIt->second + 1e-4f) {
            continue;  // stale entry
        }
        if (curr.x == egx && curr.y == egy) {
            found = true; break;
        }
        for (int k = 0; k < 8; ++k) {
            const int nx = curr.x + kDx[k];
            const int ny = curr.y + kDy[k];
            if (!IsWalkable(nx, ny)) continue;
            if (k >= 4) {
                if (!IsWalkable(curr.x + kDx[k], curr.y)) continue;
                if (!IsWalkable(curr.x, curr.y + kDy[k])) continue;
            }
            const float cost = (k < 4)
                ? 1.0f : std::sqrt(2.0f);
            const float tg = curr.g + cost;
            const int64_t nKey = Key(nx, ny);
            auto nIt = gScore.find(nKey);
            if (nIt == gScore.end() || tg < nIt->second) {
                gScore[nKey] = tg;
                cameFrom[nKey] = { curr.x, curr.y };
                open.push({ nx, ny, tg, tg + H(nx, ny) });
            }
        }
    }
    if (!found) { lua_pushnil(L); return 1; }
    std::vector<std::pair<int, int>> path;
    int cx2 = egx, cy2 = egy;
    while (!(cx2 == sgx && cy2 == sgy)) {
        path.push_back({ cx2, cy2 });
        auto cfIt = cameFrom.find(Key(cx2, cy2));
        if (cfIt == cameFrom.end()) break;
        cx2 = cfIt->second.first;
        cy2 = cfIt->second.second;
    }
    path.push_back({ sgx, sgy });
    std::reverse(path.begin(), path.end());
    // M7c30: line-of-sight string pulling. Walk the cell line
    // between two world points at sub-cell granularity + reject
    // the segment the moment any sampled cell is blocked.
    // Sub-cell step (= 0.25 cell) keeps the test conservative
    // against thin diagonal gaps without exploding sample count.
    auto LineOfSight = [&](float x0, float y0,
                           float x1, float y1) -> bool {
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1e-6f) {
            int gx, gy;
            WorldToCell(x0, y0, gx, gy);
            return IsWalkable(gx, gy);
        }
        const float step = std::min(cellW, cellH) * 0.25f;
        const int nSteps = std::max(1,
            int(std::ceil(dist / step)));
        int lastGx = INT_MIN, lastGy = INT_MIN;
        for (int i = 0; i <= nSteps; ++i) {
            const float t = float(i) / float(nSteps);
            const float wx = x0 + dx * t;
            const float wy = y0 + dy * t;
            int gx, gy;
            WorldToCell(wx, wy, gx, gy);
            if (gx != lastGx || gy != lastGy) {
                if (!IsWalkable(gx, gy)) return false;
                lastGx = gx; lastGy = gy;
            }
        }
        return true;
    };
    auto CellWorldX = [&](int g) {
        return tmTr->position.x + (float(g) + 0.5f) * cellW;
    };
    auto CellWorldY = [&](int g) {
        return tmTr->position.y + (float(g) + 0.5f) * cellH;
    };
    std::vector<std::pair<int, int>> outPath;
    if (smooth && path.size() >= 2) {
        outPath.push_back(path[0]);
        size_t i = 0;
        while (i + 1 < path.size()) {
            // Walk j from the end backwards -- first j with
            // LoS from path[i] wins, so we always skip as far
            // ahead as possible per iteration.
            size_t j = path.size() - 1;
            while (j > i + 1) {
                const float x0 = CellWorldX(path[i].first);
                const float y0 = CellWorldY(path[i].second);
                const float x1 = CellWorldX(path[j].first);
                const float y1 = CellWorldY(path[j].second);
                if (LineOfSight(x0, y0, x1, y1)) break;
                --j;
            }
            outPath.push_back(path[j]);
            i = j;
        }
    } else {
        outPath = path;
    }
    lua_createtable(L, int(outPath.size()), 0);
    for (size_t i = 0; i < outPath.size(); ++i) {
        const float wx = CellWorldX(outPath[i].first);
        const float wy = CellWorldY(outPath[i].second);
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, wx);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, wy);
        lua_setfield(L, -2, "y");
        lua_rawseti(L, -2, int(i + 1));
    }
    return 1;
}

// audio.play(path_or_guid [, volume]) -> bool. M8c2 XAudio2-
// backed playback -- many overlapping voices, optional
// volume in [0, 1] (default 1).
//
// Three input flavours, detected automatically:
//   - 36-char dashed GUID (e.g. "a1b2c3d4-e5f6-...") -> M8c4
//     asset-DB lookup. The GUID must point at an Audio
//     asset (.wav/.ogg/.mp3); any other type returns false.
//     This is the recommended form for scenes that survive
//     a file rename / move because GUID is stable.
//   - Absolute path ("C:\\..." / "/...") -> used directly.
//   - Relative path ("Sounds/coin.wav") -> resolved against
//     the project's Assets directory.
//
// Honest failure semantics survive M8c2: missing files,
// wrong-type GUIDs, malformed WAVs all return false.
static int LuaAudioPlay(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    AssetDatabase* db = host ? host->BoundAssetDatabase() : nullptr;
    const char* arg = luaL_checkstring(L, 1);
    float volume = 1.0f;
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 2)) {
        volume = float(luaL_checknumber(L, 2));
    }
    std::string resolved = arg;
    // M8c4 GUID detection: 36 chars + dashes at the
    // canonical positions (8, 13, 18, 23). Cheap, no false
    // positives in practice (real paths have slashes /
    // extensions in those positions).
    auto looksLikeGuid = [](const std::string& s) {
        if (s.size() != 36) return false;
        return s[8] == '-' && s[13] == '-' &&
               s[18] == '-' && s[23] == '-';
    };
    if (looksLikeGuid(resolved) && db) {
        AssetGuid guid = AssetGuid::FromString(resolved);
        if (!guid.IsValid()) {
            lua_pushboolean(L, 0); return 1;
        }
        const AssetEntry* entry = db->FindByGuid(guid);
        if (!entry || entry->typeName != "Audio") {
            lua_pushboolean(L, 0); return 1;
        }
        const std::string& root = db->AssetsRoot();
        const char sep =
            (!root.empty() &&
             (root.back() == '/' || root.back() == '\\'))
                ? '\0' : '/';
        resolved = root;
        if (sep) resolved.push_back(sep);
        resolved.append(entry->relPath);
    } else {
        const bool isAbs =
            (resolved.size() >= 2 && resolved[1] == ':') ||
            (!resolved.empty() && (resolved[0] == '/' ||
                                     resolved[0] == '\\'));
        if (!isAbs && db) {
            const std::string& root = db->AssetsRoot();
            if (!root.empty()) {
                const char sep =
                    (root.back() == '/' || root.back() == '\\')
                        ? '\0' : '/';
                std::string joined = root;
                if (sep) joined.push_back(sep);
                joined.append(arg);
                resolved = std::move(joined);
            }
        }
    }
    const bool ok = Audio::Play(resolved, volume);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// audio.stop() -> nil. Silences the currently-playing voice.
static int LuaAudioStop(lua_State* L) {
    (void)L;
    Audio::Stop();
    return 0;
}

// audio.play_music(path_or_guid [, volume [, loop]]) -> bool.
// M8c5 chunk-streamed music. WAV + OGG accepted; the file
// is decoded into a small ring buffer rather than loaded
// whole, so a multi-minute soundtrack stays out of the SFX
// cache. The same GUID / absolute-path / project-relative
// resolution audio.play uses applies here.
static int LuaAudioPlayMusic(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    AssetDatabase* db = host ? host->BoundAssetDatabase() : nullptr;
    const char* arg = luaL_checkstring(L, 1);
    float volume = 1.0f;
    bool  loop   = false;
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 2)) {
        volume = float(luaL_checknumber(L, 2));
    }
    if (lua_gettop(L) >= 3 && !lua_isnoneornil(L, 3)) {
        loop = lua_toboolean(L, 3) != 0;
    }
    std::string resolved = arg;
    auto looksLikeGuid = [](const std::string& s) {
        if (s.size() != 36) return false;
        return s[8] == '-' && s[13] == '-' &&
               s[18] == '-' && s[23] == '-';
    };
    if (looksLikeGuid(resolved) && db) {
        AssetGuid guid = AssetGuid::FromString(resolved);
        if (!guid.IsValid()) { lua_pushboolean(L, 0); return 1; }
        const AssetEntry* entry = db->FindByGuid(guid);
        if (!entry || entry->typeName != "Audio") {
            lua_pushboolean(L, 0); return 1;
        }
        const std::string& root = db->AssetsRoot();
        const char sep = (!root.empty() &&
            (root.back() == '/' || root.back() == '\\'))
                ? '\0' : '/';
        resolved = root;
        if (sep) resolved.push_back(sep);
        resolved.append(entry->relPath);
    } else {
        const bool isAbs =
            (resolved.size() >= 2 && resolved[1] == ':') ||
            (!resolved.empty() && (resolved[0] == '/' ||
                                     resolved[0] == '\\'));
        if (!isAbs && db) {
            const std::string& root = db->AssetsRoot();
            if (!root.empty()) {
                const char sep =
                    (root.back() == '/' || root.back() == '\\')
                        ? '\0' : '/';
                std::string joined = root;
                if (sep) joined.push_back(sep);
                joined.append(arg);
                resolved = std::move(joined);
            }
        }
    }
    const bool ok = Audio::PlayMusic(resolved, volume, loop);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// audio.stop_music() -> nil. Tears the active music stream
// down + closes its decoder handle.
static int LuaAudioStopMusic(lua_State* L) {
    (void)L;
    Audio::StopMusic();
    return 0;
}

// audio.set_bus_volume(name, volume) -> bool.
// audio.get_bus_volume(name) -> number.
// `name` is "master" | "sfx" | "music". Unknown buses
// return false / nil so callers can probe support.
static bool ParseBus(const char* name, Audio::Bus& out) {
    if (!name) return false;
    if (std::strcmp(name, "master") == 0) {
        out = Audio::Bus::Master; return true;
    }
    if (std::strcmp(name, "sfx") == 0) {
        out = Audio::Bus::Sfx; return true;
    }
    if (std::strcmp(name, "music") == 0) {
        out = Audio::Bus::Music; return true;
    }
    return false;
}
static int LuaAudioSetBusVolume(lua_State* L) {
    Audio::Bus bus;
    if (!ParseBus(luaL_checkstring(L, 1), bus)) {
        lua_pushboolean(L, 0); return 1;
    }
    const float vol = float(luaL_checknumber(L, 2));
    Audio::SetBusVolume(bus, vol);
    lua_pushboolean(L, 1);
    return 1;
}
static int LuaAudioGetBusVolume(lua_State* L) {
    Audio::Bus bus;
    if (!ParseBus(luaL_checkstring(L, 1), bus)) {
        lua_pushnil(L); return 1;
    }
    lua_pushnumber(L, Audio::GetBusVolume(bus));
    return 1;
}

// M8c11 per-voice control. Same path / GUID resolution as
// audio.play. Returns the voice handle (number) on success;
// nil on failure -- maps to Lua's truthy / falsy in the
// natural way so `if h then ...` works.
static std::string ResolveAudioPath(ScriptHost* host,
                                     const char* arg);
static int LuaAudioSpawn(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* arg = luaL_checkstring(L, 1);
    float volume = 1.0f;
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 2)) {
        volume = float(luaL_checknumber(L, 2));
    }
    const std::string resolved = ResolveAudioPath(host, arg);
    if (resolved.empty()) { lua_pushnil(L); return 1; }
    const Audio::VoiceHandle h = Audio::Spawn(resolved, volume);
    if (h == 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, lua_Integer(h));
    return 1;
}
static int LuaAudioStopVoice(lua_State* L) {
    const lua_Integer h = luaL_checkinteger(L, 1);
    lua_pushboolean(L,
        Audio::StopVoice(Audio::VoiceHandle(h)) ? 1 : 0);
    return 1;
}
static int LuaAudioPauseVoice(lua_State* L) {
    const lua_Integer h = luaL_checkinteger(L, 1);
    lua_pushboolean(L,
        Audio::PauseVoice(Audio::VoiceHandle(h)) ? 1 : 0);
    return 1;
}
static int LuaAudioResumeVoice(lua_State* L) {
    const lua_Integer h = luaL_checkinteger(L, 1);
    lua_pushboolean(L,
        Audio::ResumeVoice(Audio::VoiceHandle(h)) ? 1 : 0);
    return 1;
}
static int LuaAudioSetVoiceVolume(lua_State* L) {
    const lua_Integer h   = luaL_checkinteger(L, 1);
    const float       vol = float(luaL_checknumber(L, 2));
    lua_pushboolean(L,
        Audio::SetVoiceVolume(Audio::VoiceHandle(h), vol)
            ? 1 : 0);
    return 1;
}
static int LuaAudioIsVoicePlaying(lua_State* L) {
    const lua_Integer h = luaL_checkinteger(L, 1);
    lua_pushboolean(L,
        Audio::IsVoicePlaying(Audio::VoiceHandle(h)) ? 1 : 0);
    return 1;
}

// M8c13 audio.play_positional(path_or_guid, vol, pan [, freq_ratio]).
// Same SFX cache path as audio.play, but the caller picks the
// stereo pan + (optional) pitch shift -- the building block for
// game-side doppler. freq_ratio defaults to 1.0 (no shift); the
// engine clamps to [0.25, 2.0]. Pan is in [-1, 1].
static int LuaAudioPlayPositional(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* arg = luaL_checkstring(L, 1);
    const float vol = float(luaL_checknumber(L, 2));
    const float pan = float(luaL_checknumber(L, 3));
    float freqRatio = 1.0f;
    if (lua_gettop(L) >= 4 && !lua_isnoneornil(L, 4)) {
        freqRatio = float(luaL_checknumber(L, 4));
    }
    const std::string resolved = ResolveAudioPath(host, arg);
    if (resolved.empty()) {
        lua_pushboolean(L, 0); return 1;
    }
    lua_pushboolean(L,
        Audio::PlayPositional(resolved, vol, pan, freqRatio) ? 1 : 0);
    return 1;
}
static int LuaAudioSetReverbWet(lua_State* L) {
    const float wet = float(luaL_checknumber(L, 1));
    Audio::SetReverbWet(wet);
    return 0;
}
static int LuaAudioGetReverbWet(lua_State* L) {
    lua_pushnumber(L, Audio::GetReverbWet());
    return 1;
}

// Path / GUID resolution used by spawn -- same rules audio.play
// applies. Returns an empty string on a missing GUID / wrong
// type GUID; the Spawn binding turns that into a nil result.
static std::string ResolveAudioPath(ScriptHost* host,
                                     const char* arg) {
    AssetDatabase* db = host ? host->BoundAssetDatabase() : nullptr;
    std::string resolved = arg ? arg : "";
    auto looksLikeGuid = [](const std::string& s) {
        if (s.size() != 36) return false;
        return s[8] == '-' && s[13] == '-' &&
               s[18] == '-' && s[23] == '-';
    };
    if (looksLikeGuid(resolved) && db) {
        AssetGuid guid = AssetGuid::FromString(resolved);
        if (!guid.IsValid()) return "";
        const AssetEntry* entry = db->FindByGuid(guid);
        if (!entry || entry->typeName != "Audio") return "";
        const std::string& root = db->AssetsRoot();
        const char sep = (!root.empty() &&
            (root.back() == '/' || root.back() == '\\'))
                ? '\0' : '/';
        std::string out = root;
        if (sep) out.push_back(sep);
        out.append(entry->relPath);
        return out;
    }
    const bool isAbs =
        (resolved.size() >= 2 && resolved[1] == ':') ||
        (!resolved.empty() && (resolved[0] == '/' ||
                                 resolved[0] == '\\'));
    if (!isAbs && db) {
        const std::string& root = db->AssetsRoot();
        if (!root.empty()) {
            const char sep =
                (root.back() == '/' || root.back() == '\\')
                    ? '\0' : '/';
            std::string joined = root;
            if (sep) joined.push_back(sep);
            joined.append(arg ? arg : "");
            return joined;
        }
    }
    return resolved;
}

// M9c3 particle.* helpers. Same FindByName + GetComponent
// pattern every other component-targeted API uses. A nil
// component lookup just returns the binding's failure value
// (false / 0) so a stale entity name doesn't raise.
static ParticleSystemComponent* FindParticleSystem(
        ScriptHost* host, const char* entityName) {
    if (!host || !host->BoundScene() || !entityName) return nullptr;
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) return nullptr;
    return e->GetComponent<ParticleSystemComponent>();
}

// particle.emit(entity, count?) -> int actually emitted.
// Pulls the entity's Transform position, samples the same
// range fields the auto-emit path uses, and fills slots
// directly into the live pool until either `count` slots are
// taken or the pool's maxParticles cap is reached. count
// defaults to 1; emission Rate / playOnStart / loop are
// untouched -- this is an extra burst on top of whatever the
// component is already doing.
static int LuaParticleEmit(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    int count = 1;
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 2)) {
        count = int(luaL_checkinteger(L, 2));
    }
    if (count <= 0) { lua_pushinteger(L, 0); return 1; }
    ParticleSystemComponent* ps = FindParticleSystem(host, name);
    if (!ps) { lua_pushinteger(L, 0); return 1; }
    Entity* e = host->BoundScene()->FindByName(name);
    TransformComponent* tr = e ? e->GetComponent<TransformComponent>()
                                : nullptr;
    if (!tr) { lua_pushinteger(L, 0); return 1; }
    // Lazy-resize the pool so emit-before-the-first-Tick still
    // works. The Tick path uses the same idiom.
    if (ps->maxParticles < 1) ps->maxParticles = 1;
    if (int(ps->_pool.size()) != ps->maxParticles) {
        ps->_pool.assign(size_t(ps->maxParticles),
            ParticleSystemComponent::Particle{});
        ps->_liveCount = 0;
    }
    // Seed lazily the same way Tick does so two adjacent
    // particle.emit calls on a brand-new component don't
    // emit identical sequences.
    if (ps->_rng == 0) ps->_rng = 0x9E3779B9u + 1u;
    auto xs = [](std::uint32_t& s) -> std::uint32_t {
        if (s == 0) s = 0x9E3779B9u;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    };
    auto frand = [&](std::uint32_t& s, float lo, float hi) {
        const std::uint32_t r = xs(s);
        const float u = float(r) * (1.0f / 4294967296.0f);
        return lo + (hi - lo) * u;
    };
    int emitted = 0;
    while (emitted < count &&
           ps->_liveCount < ps->maxParticles) {
        auto& p = ps->_pool[size_t(ps->_liveCount++)];
        ++ps->_emittedTotal;
        ++emitted;
        p.pos = { tr->position.x, tr->position.y };
        const float ang = frand(ps->_rng,
            ps->dirAngleMinDeg, ps->dirAngleMaxDeg) *
            (3.14159265358979323846f / 180.0f);
        const float spd = frand(ps->_rng,
            ps->startSpeedMin, ps->startSpeedMax);
        p.vel = { std::cos(ang) * spd,
                  std::sin(ang) * spd };
        p.age      = 0.0f;
        p.lifetime = frand(ps->_rng,
            ps->lifetimeMin,
            std::max(ps->lifetimeMin, ps->lifetimeMax));
        p.size     = frand(ps->_rng,
            ps->startSizeMin, ps->startSizeMax);
    }
    lua_pushinteger(L, emitted);
    return 1;
}

// particle.clear(entity) -> bool. Wipes the live pool without
// touching emission settings. Useful when an explosion finished
// + the next state shouldn't carry over residual smoke.
static int LuaParticleClear(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    ParticleSystemComponent* ps = FindParticleSystem(host, name);
    if (!ps) { lua_pushboolean(L, 0); return 1; }
    ps->_liveCount       = 0;
    ps->_emitAccum       = 0.0f;
    ps->_emittedTotal    = 0;
    ps->_elapsed         = 0.0f;
    ps->_burstNextIndex  = 0;
    lua_pushboolean(L, 1);
    return 1;
}

// particle.set_emission_rate(entity, rate) -> bool. Live tune
// the per-second rate. 0 disables continuous emission while
// leaving particle.emit() bursts untouched.
static int LuaParticleSetEmissionRate(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    const float rate = float(luaL_checknumber(L, 2));
    ParticleSystemComponent* ps = FindParticleSystem(host, name);
    if (!ps) { lua_pushboolean(L, 0); return 1; }
    ps->emissionRate = (rate < 0.0f) ? 0.0f : rate;
    lua_pushboolean(L, 1);
    return 1;
}

// particle.get_count(entity) -> int. Live particle count;
// useful for pacing the next burst or detecting "finished".
static int LuaParticleGetCount(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    ParticleSystemComponent* ps = FindParticleSystem(host, name);
    lua_pushinteger(L, ps ? ps->_liveCount : 0);
    return 1;
}

// particle.set_playing(entity, bool) -> bool. Toggles the
// auto-emit + integrate path. Setting to false freezes new
// emissions; setting back to true (without resetting) picks
// up where the emitter left off. particle.clear is the
// "wipe to zero" sibling.
static int LuaParticleSetPlaying(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    const bool on    = (lua_toboolean(L, 2) != 0);
    ParticleSystemComponent* ps = FindParticleSystem(host, name);
    if (!ps) { lua_pushboolean(L, 0); return 1; }
    // _started gates the rising-edge auto-play in Tick. Flip
    // it directly so the runtime doesn't fire playOnStart
    // again when we toggle back on.
    ps->_started    = on;
    ps->playOnStart = on;
    lua_pushboolean(L, 1);
    return 1;
}

// particle.is_playing(entity) -> bool. True when the
// component's _started flag is set + playOnStart says emission
// should continue. Mirrors the auto-emit gate the Tick uses.
static int LuaParticleIsPlaying(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    ParticleSystemComponent* ps = FindParticleSystem(host, name);
    lua_pushboolean(L,
        (ps && ps->_started && ps->playOnStart) ? 1 : 0);
    return 1;
}

// M9c11 camera.* helpers.
static CameraComponent* FindCamera(ScriptHost* host,
                                    const char* entityName) {
    if (!host || !host->BoundScene() || !entityName) return nullptr;
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) return nullptr;
    return e->GetComponent<CameraComponent>();
}

// camera.set_zoom(entity, zoom) -> bool. Sets the component's
// zoom in pixels per world unit. Negative / zero values clamp
// to 0.01 so a typo doesn't divide-by-zero the projection.
static int LuaCameraSetZoom(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    const float z    = float(luaL_checknumber(L, 2));
    CameraComponent* cam = FindCamera(host, name);
    if (!cam) { lua_pushboolean(L, 0); return 1; }
    cam->zoom = (z < 0.01f) ? 0.01f : z;
    lua_pushboolean(L, 1);
    return 1;
}

// camera.get_zoom(entity) -> num or nil when entity has no
// CameraComponent.
static int LuaCameraGetZoom(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    CameraComponent* cam = FindCamera(host, name);
    if (!cam) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, cam->zoom);
    return 1;
}

// camera.set_priority(entity, priority) -> bool. Higher wins
// when multiple cameras coexist; the runtime re-resolves the
// active camera every frame, so script-driven swaps land
// immediately.
static int LuaCameraSetPriority(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    const int   p    = int(luaL_checkinteger(L, 2));
    CameraComponent* cam = FindCamera(host, name);
    if (!cam) { lua_pushboolean(L, 0); return 1; }
    cam->priority = p;
    lua_pushboolean(L, 1);
    return 1;
}

// camera.get_priority(entity) -> int or nil.
static int LuaCameraGetPriority(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    CameraComponent* cam = FindCamera(host, name);
    if (!cam) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, cam->priority);
    return 1;
}

// camera.get_active() -> str entity name (or "" when no
// CameraComponent is enabled). Walks the scene + picks the
// highest-priority enabled CameraComponent; ties break in
// traversal order.
static int LuaCameraGetActive(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    Scene* scene = host ? host->BoundScene() : nullptr;
    if (!scene) { lua_pushstring(L, ""); return 1; }
    Entity*  best     = nullptr;
    int      bestPrio = 0;
    auto walk = [&](auto& self,
                    const std::vector<EntityId>& roots) -> void {
        std::vector<EntityId> stack(roots.begin(), roots.end());
        while (!stack.empty()) {
            EntityId id = stack.back(); stack.pop_back();
            Entity* e = scene->Get(id);
            if (!e) continue;
            for (EntityId childId : e->children) stack.push_back(childId);
            if (!e->enabled) continue;
            auto* cam = e->GetComponent<CameraComponent>();
            if (!cam) continue;
            if (!best || cam->priority > bestPrio) {
                best     = e;
                bestPrio = cam->priority;
            }
        }
    };
    walk(walk, scene->Roots());
    lua_pushstring(L, best ? best->name.c_str() : "");
    return 1;
}

// M9c15 scene.load(path_or_guid) -> bool. Queues a scene
// swap; the host actually executes it at the end of the
// current frame so the script callback iterating live
// entities doesn't see them disappear mid-loop. Accepts the
// same path resolution audio.* does: 36-char dashed GUID
// looked up against an Audio... wait, no -- scenes get a
// simpler rule. A relative path joins to AssetDatabase's
// AssetsRoot(); a path that already starts with "Assets/" or
// is absolute is used verbatim.
static int LuaSceneLoad(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host) { lua_pushboolean(L, 0); return 1; }
    const char* arg = luaL_checkstring(L, 1);
    if (!arg || !*arg) { lua_pushboolean(L, 0); return 1; }
    std::string resolved = arg;
    const bool isAbs =
        (resolved.size() >= 2 && resolved[1] == ':') ||
        (!resolved.empty() && (resolved[0] == '/' ||
                                resolved[0] == '\\'));
    if (!isAbs &&
        resolved.compare(0, 7, "Assets/") != 0 &&
        host->BoundAssetDatabase()) {
        const std::string& root =
            host->BoundAssetDatabase()->AssetsRoot();
        if (!root.empty()) {
            const char sep =
                (root.back() == '/' || root.back() == '\\')
                    ? '\0' : '/';
            std::string joined = root;
            if (sep) joined.push_back(sep);
            joined.append(arg);
            resolved = std::move(joined);
        }
    }
    host->SetPendingSceneLoad(resolved);
    lua_pushboolean(L, 1);
    return 1;
}

// scene.overlap_circle(cx, cy, r) -> table of entity names.
// Same area-query shape as overlap_box but with a virtual
// circle. Box colliders use the nearest-point-on-box clamp,
// circle colliders use centre distance vs summed radii.
static int LuaSceneOverlapCircle(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    lua_newtable(L);
    if (!host || !host->BoundScene()) return 1;
    const float cx = float(luaL_checknumber(L, 1));
    const float cy = float(luaL_checknumber(L, 2));
    const float r  = float(luaL_checknumber(L, 3));
    const float r2 = r * r;
    Scene* scene = host->BoundScene();
    int idx = 1;
    std::vector<EntityId> stack(scene->Roots().begin(),
                                scene->Roots().end());
    while (!stack.empty()) {
        EntityId id = stack.back(); stack.pop_back();
        Entity* e = scene->Get(id);
        if (!e) continue;
        for (EntityId childId : e->children) stack.push_back(childId);
        if (!e->enabled) continue;
        auto* tr = e->GetComponent<TransformComponent>();
        if (!tr) continue;
        auto* bc = e->GetComponent<BoxColliderComponent>();
        auto* cc = e->GetComponent<CircleColliderComponent>();
        if (!bc && !cc) continue;
        bool hit = false;
        if (bc) {
            const float halfX = bc->size.x * 0.5f * tr->scale.x;
            const float halfY = bc->size.y * 0.5f * tr->scale.y;
            const float ex = tr->position.x + bc->offset.x;
            const float ey = tr->position.y + bc->offset.y;
            const float rotRad = tr->rotation
                * 3.14159265358979323846f / 180.0f;
            const float cosR = std::cos(rotRad);
            const float sinR = std::sin(rotRad);
            // Rotate the world-space query-circle centre into the
            // box's local frame, then run the regular axis-
            // aligned clamp + radius compare.
            const float relX = cx - ex;
            const float relY = cy - ey;
            const float lx =  cosR * relX + sinR * relY;
            const float ly = -sinR * relX + cosR * relY;
            const float qx = std::clamp(lx, -halfX, halfX);
            const float qy = std::clamp(ly, -halfY, halfY);
            const float ddx = lx - qx;
            const float ddy = ly - qy;
            hit = (ddx * ddx + ddy * ddy) <= r2;
        } else {
            const float scaleAvg =
                (tr->scale.x + tr->scale.y) * 0.5f;
            const float ex = tr->position.x + cc->offset.x;
            const float ey = tr->position.y + cc->offset.y;
            const float er = cc->radius * scaleAvg;
            const float ddx = cx - ex;
            const float ddy = cy - ey;
            const float sum = r + er;
            hit = (ddx * ddx + ddy * ddy) <= sum * sum;
        }
        if (hit) {
            lua_pushstring(L, e->name.c_str());
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

// scene.find_by_component(type_name) -> table of strings. DFS
// walks the entity graph + returns every entity whose
// components vector contains an entry whose TypeName() matches.
// Lets scripts iterate "every entity with an Animator" /
// "every Tilemap" / "every Script" without keeping a manual
// registry in Lua. Returns an empty table for unknown types or
// when nothing matches.
static int LuaSceneFindByComponent(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    lua_newtable(L);
    if (!host || !host->BoundScene()) return 1;
    const char* typeName = luaL_checkstring(L, 1);
    Scene* scene = host->BoundScene();
    int idx = 1;
    std::vector<EntityId> stack(scene->Roots().begin(),
                                scene->Roots().end());
    while (!stack.empty()) {
        EntityId id = stack.back(); stack.pop_back();
        Entity* e = scene->Get(id);
        if (!e) continue;
        for (EntityId childId : e->children) stack.push_back(childId);
        for (const auto& c : e->components) {
            if (c && std::strcmp(c->TypeName(), typeName) == 0) {
                lua_pushstring(L, e->name.c_str());
                lua_rawseti(L, -2, idx++);
                break;
            }
        }
    }
    return 1;
}

// scene.entity_at(x, y) -> string. Walks every enabled entity
// that carries a SpriteRenderer + tests its world-space AABB
// against the (x, y) point. Returns the topmost hit by
// orderInLayer (with simple sortingLayer-string equality as a
// tie-breaker fence) or the empty string when nothing is under
// the point. Useful for mouse-picking in Lua-driven editor
// tooling + click-to-select gameplay logic.
static int LuaSceneEntityAt(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushstring(L, ""); return 1;
    }
    const float wx = float(luaL_checknumber(L, 1));
    const float wy = float(luaL_checknumber(L, 2));
    Scene* scene = host->BoundScene();
    // Pick: keep the entity with the highest orderInLayer among
    // hits; the empty pick stays "no entity".
    const Entity* best = nullptr;
    int bestOrder = 0;
    // Walk every entity via the same DFS the other helpers use.
    std::vector<EntityId> stack(scene->Roots().begin(),
                                scene->Roots().end());
    while (!stack.empty()) {
        EntityId id = stack.back(); stack.pop_back();
        Entity* e = scene->Get(id);
        if (!e) continue;
        for (EntityId childId : e->children) stack.push_back(childId);
        if (!e->enabled) continue;
        auto* tr = e->GetComponent<TransformComponent>();
        auto* sp = e->GetComponent<SpriteRendererComponent>();
        if (!tr || !sp) continue;
        // M7c15: world point routed into the sprite's local
        // frame so the pick honours Transform.rotation. Z is
        // ignored because the editor is 2D-first.
        const float halfX =
            sp->size.x * 0.5f * tr->scale.x;
        const float halfY =
            sp->size.y * 0.5f * tr->scale.y;
        const float relX = wx - tr->position.x;
        const float relY = wy - tr->position.y;
        const float rotRad = tr->rotation
            * 3.14159265358979323846f / 180.0f;
        const float cosR = std::cos(rotRad);
        const float sinR = std::sin(rotRad);
        const float lx =  cosR * relX + sinR * relY;
        const float ly = -sinR * relX + cosR * relY;
        if (std::fabs(lx) > halfX || std::fabs(ly) > halfY)
            continue;
        if (!best || sp->orderInLayer >= bestOrder) {
            best = e;
            bestOrder = sp->orderInLayer;
        }
    }
    lua_pushstring(L, best ? best->name.c_str() : "");
    return 1;
}

// scene.destroy_self. Fires from inside a lifecycle hook +
// destroys whichever entity the host is currently dispatching
// to. Outside a dispatch the call is a silent no-op so a stray
// `scene.destroy_self()` from top-level code can't wipe the
// scene. Re-uses FireScriptDestroyTree so children + OnDestroy
// notifications follow the same path scene.destroy_entity does.
static int LuaSceneDestroySelf(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushboolean(L, 0); return 1;
    }
    const std::string& name = host->DispatchEntity();
    if (name.empty()) { lua_pushboolean(L, 0); return 1; }
    Scene* scene = host->BoundScene();
    Entity* e = scene->FindByName(name);
    if (!e) { lua_pushboolean(L, 0); return 1; }
    const EntityId id = e->id;
    FireScriptDestroyTree(host, scene, id);
    scene->DestroyEntity(id);
    lua_pushboolean(L, 1);
    return 1;
}

// scene.parent_of / children_of / set_parent. Lets scripts walk
// the entity hierarchy + reparent at runtime. parent_of returns
// the parent's name string (empty string when the entity is a
// root or missing); children_of returns a 1-indexed table of
// child names. set_parent wraps Scene::Reparent + understands an
// empty parent argument as "make this entity a root".
static int LuaSceneParentOf(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushstring(L, ""); return 1;
    }
    Scene* scene = host->BoundScene();
    Entity* e = scene->FindByName(luaL_checkstring(L, 1));
    if (!e || e->parent == kInvalidEntityId) {
        lua_pushstring(L, ""); return 1;
    }
    Entity* p = scene->Get(e->parent);
    lua_pushstring(L, p ? p->name.c_str() : "");
    return 1;
}
static int LuaSceneChildrenOf(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    lua_newtable(L);
    if (!host || !host->BoundScene()) return 1;
    Scene* scene = host->BoundScene();
    Entity* e = scene->FindByName(luaL_checkstring(L, 1));
    if (!e) return 1;
    int idx = 1;
    for (EntityId childId : e->children) {
        Entity* c = scene->Get(childId);
        if (!c) continue;
        lua_pushstring(L, c->name.c_str());
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}
static int LuaSceneSetParent(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushboolean(L, 0); return 1;
    }
    Scene* scene = host->BoundScene();
    Entity* e = scene->FindByName(luaL_checkstring(L, 1));
    if (!e) { lua_pushboolean(L, 0); return 1; }
    const char* parentName = luaL_optstring(L, 2, nullptr);
    EntityId newParent = kInvalidEntityId;
    if (parentName && parentName[0] != '\0') {
        // Missing parent name is a typo, not a "make me root"
        // signal -- callers that want a root should pass "" or
        // nil explicitly. Distinguish by returning false here.
        Entity* p = scene->FindByName(parentName);
        if (!p) { lua_pushboolean(L, 0); return 1; }
        newParent = p->id;
    }
    scene->Reparent(e->id, newParent);
    lua_pushboolean(L, 1);
    return 1;
}

// scene.set_enabled / is_enabled. Flipping enabled triggers the
// M6c17 OnEnable / OnDisable edge detection on the next
// TickScripts pass, so a script that toggles its own host
// observes the lifecycle hooks fire as a side-effect.
static int LuaSceneSetEnabled(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushboolean(L, 0); return 1;
    }
    Entity* e = host->BoundScene()->FindByName(
        luaL_checkstring(L, 1));
    if (!e) { lua_pushboolean(L, 0); return 1; }
    e->enabled = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}
static int LuaSceneIsEnabled(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushboolean(L, 0); return 1;
    }
    Entity* e = host->BoundScene()->FindByName(
        luaL_checkstring(L, 1));
    lua_pushboolean(L, e && e->enabled);
    return 1;
}

// scene.add_component / has_component / remove_component. Each
// callback resolves the entity by name through BoundScene and
// returns `false` (silent no-op) when the lookup fails so scripts
// can probe for the right entity without raising errors.
static int LuaSceneAddComponent(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushboolean(L, 0); return 1;
    }
    const char* entityName = luaL_checkstring(L, 1);
    const char* typeName   = luaL_checkstring(L, 2);
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) { lua_pushboolean(L, 0); return 1; }
    auto& reg = ComponentRegistry::Get();
    auto comp = reg.Create(typeName);
    if (!comp) { lua_pushboolean(L, 0); return 1; }
    e->components.push_back(std::move(comp));
    lua_pushboolean(L, 1);
    return 1;
}
static int LuaSceneHasComponent(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushboolean(L, 0); return 1;
    }
    const char* entityName = luaL_checkstring(L, 1);
    const char* typeName   = luaL_checkstring(L, 2);
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) { lua_pushboolean(L, 0); return 1; }
    for (const auto& c : e->components) {
        if (c && std::strcmp(c->TypeName(), typeName) == 0) {
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}
static int LuaSceneRemoveComponent(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    if (!host || !host->BoundScene()) {
        lua_pushboolean(L, 0); return 1;
    }
    const char* entityName = luaL_checkstring(L, 1);
    const char* typeName   = luaL_checkstring(L, 2);
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) { lua_pushboolean(L, 0); return 1; }
    // Erase the first matching component. Multiple components of
    // the same type are unusual but allowed in the data model;
    // a subsequent remove call clears the next slot.
    for (size_t i = 0; i < e->components.size(); ++i) {
        const auto& c = e->components[i];
        if (c && std::strcmp(c->TypeName(), typeName) == 0) {
            e->components.erase(e->components.begin() + i);
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int LuaSceneAllEntities(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    Scene* scene = host ? host->BoundScene() : nullptr;
    lua_newtable(L);
    if (!scene) return 1;
    int idx = 1;
    std::vector<EntityId> stack(scene->Roots().begin(),
                                scene->Roots().end());
    while (!stack.empty()) {
        EntityId id = stack.back(); stack.pop_back();
        Entity* e = scene->Get(id);
        if (!e) continue;
        lua_pushstring(L, e->name.c_str());
        lua_rawseti(L, -2, idx++);
        for (EntityId childId : e->children) stack.push_back(childId);
    }
    return 1;
}

// time.delta / time.elapsed / time.frame_count / time.fps.
// delta() + fps() route through ImGui's own per-frame stats
// (ImGui already maintains a smoothed Framerate alongside the
// raw DeltaTime); elapsed() + frame_count() come from the host's
// own running totals (Tick(dt) bumps them once per editor frame).
static int LuaTimeDelta(lua_State* L) {
    lua_pushnumber(L, ImGui::GetIO().DeltaTime);
    return 1;
}
static int LuaTimeElapsed(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    lua_pushnumber(L, host ? host->Elapsed() : 0.0);
    return 1;
}
static int LuaTimeFrameCount(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    lua_pushinteger(L,
        host ? lua_Integer(host->FrameCount()) : lua_Integer(0));
    return 1;
}
static int LuaTimeFps(lua_State* L) {
    lua_pushnumber(L, ImGui::GetIO().Framerate);
    return 1;
}

// Same pattern again for SpriteRendererComponent. Entities without
// a renderer are common (light-only / logic-only entities) so the
// nullptr return is the expected "silent no-op" path.
static SpriteRendererComponent* FindSpriteRenderer(
        ScriptHost* host, const char* entityName) {
    if (!host || !host->BoundScene() || !entityName) return nullptr;
    Entity* e = host->BoundScene()->FindByName(entityName);
    if (!e) return nullptr;
    return e->GetComponent<SpriteRendererComponent>();
}

// sprite.set_color / get_color / set_alpha / get_alpha /
// set_sub_sprite / get_sub_sprite / set_flip_x / get_flip_x /
// set_flip_y / get_flip_y / set_size / get_size / set_order /
// get_order. Sprite GUID swaps + drawMode + sortingLayer stay
// editor-side for now -- the inspector handles those cleanly and
// runtime scripts rarely need to touch them.
static int LuaSpriteSetColor(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    if (!s) return 0;
    s->color = NaraGame::Color{
        float(luaL_checknumber(L, 2)),
        float(luaL_checknumber(L, 3)),
        float(luaL_checknumber(L, 4)),
        float(luaL_checknumber(L, 5))};
    return 0;
}
static int LuaSpriteGetColor(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    if (!s) {
        lua_pushnumber(L, 1); lua_pushnumber(L, 1);
        lua_pushnumber(L, 1); lua_pushnumber(L, 1);
        return 4;
    }
    lua_pushnumber(L, s->color.r);
    lua_pushnumber(L, s->color.g);
    lua_pushnumber(L, s->color.b);
    lua_pushnumber(L, s->color.a);
    return 4;
}
// Convenience getters / setters for the alpha channel -- the
// "fade in / fade out" pattern doesn't need to round-trip rgb.
static int LuaSpriteSetAlpha(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    if (!s) return 0;
    s->color.a = float(luaL_checknumber(L, 2));
    return 0;
}
static int LuaSpriteGetAlpha(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    lua_pushnumber(L, s ? s->color.a : 1.0f);
    return 1;
}
static int LuaSpriteSetSubSprite(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    if (!s) return 0;
    s->subSprite = luaL_checkstring(L, 2);
    return 0;
}
static int LuaSpriteGetSubSprite(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    lua_pushstring(L, s ? s->subSprite.c_str() : "");
    return 1;
}
static int LuaSpriteSetFlipX(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    if (!s) return 0;
    s->flipX = lua_toboolean(L, 2) != 0;
    return 0;
}
static int LuaSpriteGetFlipX(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    lua_pushboolean(L, s && s->flipX);
    return 1;
}
static int LuaSpriteSetFlipY(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    if (!s) return 0;
    s->flipY = lua_toboolean(L, 2) != 0;
    return 0;
}
static int LuaSpriteGetFlipY(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    lua_pushboolean(L, s && s->flipY);
    return 1;
}
static int LuaSpriteSetSize(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    if (!s) return 0;
    s->size.x = float(luaL_checknumber(L, 2));
    s->size.y = float(luaL_checknumber(L, 3));
    return 0;
}
static int LuaSpriteGetSize(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    if (!s) { lua_pushnumber(L, 1); lua_pushnumber(L, 1); return 2; }
    lua_pushnumber(L, s->size.x);
    lua_pushnumber(L, s->size.y);
    return 2;
}
static int LuaSpriteSetOrder(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    if (!s) return 0;
    s->orderInLayer = int(luaL_checkinteger(L, 2));
    return 0;
}
static int LuaSpriteGetOrder(lua_State* L) {
    auto* s = FindSpriteRenderer(HostFromUpvalue(L),
                                  luaL_checkstring(L, 1));
    lua_pushinteger(L, s ? s->orderInLayer : 0);
    return 1;
}

// transform.set_position / get_position / set_rotation /
// get_rotation / set_scale / get_scale / translate. Mirrors the
// animator API shape: setters return nothing; vector getters
// return component values in stack order; missing entities +
// missing TransformComponents are silent no-ops.
static int LuaTransformSetPosition(lua_State* L) {
    auto* t = FindTransform(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!t) return 0;
    t->position = NaraGame::Vec3{
        float(luaL_checknumber(L, 2)),
        float(luaL_checknumber(L, 3)),
        float(luaL_checknumber(L, 4))};
    return 0;
}
static int LuaTransformGetPosition(lua_State* L) {
    auto* t = FindTransform(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!t) {
        lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0);
        return 3;
    }
    lua_pushnumber(L, t->position.x);
    lua_pushnumber(L, t->position.y);
    lua_pushnumber(L, t->position.z);
    return 3;
}
static int LuaTransformSetRotation(lua_State* L) {
    auto* t = FindTransform(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!t) return 0;
    t->rotation = float(luaL_checknumber(L, 2));
    return 0;
}
static int LuaTransformGetRotation(lua_State* L) {
    auto* t = FindTransform(HostFromUpvalue(L), luaL_checkstring(L, 1));
    lua_pushnumber(L, t ? t->rotation : 0.0f);
    return 1;
}
static int LuaTransformSetScale(lua_State* L) {
    auto* t = FindTransform(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!t) return 0;
    t->scale = NaraGame::Vec3{
        float(luaL_checknumber(L, 2)),
        float(luaL_checknumber(L, 3)),
        float(luaL_checknumber(L, 4))};
    return 0;
}
static int LuaTransformGetScale(lua_State* L) {
    auto* t = FindTransform(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!t) {
        lua_pushnumber(L, 1); lua_pushnumber(L, 1); lua_pushnumber(L, 1);
        return 3;
    }
    lua_pushnumber(L, t->scale.x);
    lua_pushnumber(L, t->scale.y);
    lua_pushnumber(L, t->scale.z);
    return 3;
}
// transform.translate(entity, dx, dy, dz) -- convenience for the
// common "advance by (dx, dy, dz) this frame" pattern so user
// scripts don't have to round-trip the full position triple.
static int LuaTransformTranslate(lua_State* L) {
    auto* t = FindTransform(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!t) return 0;
    t->position.x += float(luaL_checknumber(L, 2));
    t->position.y += float(luaL_checknumber(L, 3));
    t->position.z += float(luaL_checknumber(L, 4));
    return 0;
}

// rigidbody.set_velocity / get_velocity / add_force /
// set_gravity / get_gravity / set_drag / get_drag. All vector
// values are 2D world units / second. add_force is an impulse
// (instantaneously bumps velocity); ongoing forces should set
// gravity instead. Missing Rigidbody2D component is a silent
// no-op; getters return zero/empty defaults.
static int LuaRigidbodySetVelocity(lua_State* L) {
    auto* r = FindRigidbody(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!r) return 0;
    r->velocity.x = float(luaL_checknumber(L, 2));
    r->velocity.y = float(luaL_checknumber(L, 3));
    return 0;
}
static int LuaRigidbodyGetVelocity(lua_State* L) {
    auto* r = FindRigidbody(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!r) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
    lua_pushnumber(L, r->velocity.x);
    lua_pushnumber(L, r->velocity.y);
    return 2;
}
static int LuaRigidbodyAddForce(lua_State* L) {
    auto* r = FindRigidbody(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!r) return 0;
    r->velocity.x += float(luaL_checknumber(L, 2));
    r->velocity.y += float(luaL_checknumber(L, 3));
    return 0;
}
static int LuaRigidbodySetGravity(lua_State* L) {
    auto* r = FindRigidbody(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!r) return 0;
    r->gravity.x = float(luaL_checknumber(L, 2));
    r->gravity.y = float(luaL_checknumber(L, 3));
    return 0;
}
static int LuaRigidbodyGetGravity(lua_State* L) {
    auto* r = FindRigidbody(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!r) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
    lua_pushnumber(L, r->gravity.x);
    lua_pushnumber(L, r->gravity.y);
    return 2;
}
static int LuaRigidbodySetDrag(lua_State* L) {
    auto* r = FindRigidbody(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!r) return 0;
    r->drag = float(luaL_checknumber(L, 2));
    if (r->drag < 0.0f) r->drag = 0.0f;
    return 0;
}
static int LuaRigidbodyGetDrag(lua_State* L) {
    auto* r = FindRigidbody(HostFromUpvalue(L), luaL_checkstring(L, 1));
    lua_pushnumber(L, r ? r->drag : 0.0);
    return 1;
}

// animator.set_bool / get_bool / set_float / get_float / set_int /
// get_int / set_trigger / set_vec2 / get_vec2 / set_color /
// get_color / set_string / get_string. Setters return nothing;
// getters return the requested value (Vec2 returns x, y -- Color
// returns r, g, b, a -- in stack order). Missing entities + missing
// AnimatorComponents are silent no-ops; missing parameter entries
// fall back to a zero / empty default on read.
static int LuaAnimatorSetBool(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramBools[luaL_checkstring(L, 2)] = lua_toboolean(L, 3) != 0;
    return 0;
}
static int LuaAnimatorGetBool(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushboolean(L, 0); return 1; }
    auto it = an->paramBools.find(luaL_checkstring(L, 2));
    lua_pushboolean(L, it != an->paramBools.end() && it->second);
    return 1;
}
static int LuaAnimatorSetFloat(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramFloats[luaL_checkstring(L, 2)] =
        float(luaL_checknumber(L, 3));
    return 0;
}
static int LuaAnimatorGetFloat(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushnumber(L, 0.0); return 1; }
    auto it = an->paramFloats.find(luaL_checkstring(L, 2));
    lua_pushnumber(L,
        it != an->paramFloats.end() ? it->second : 0.0f);
    return 1;
}
static int LuaAnimatorSetInt(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramInts[luaL_checkstring(L, 2)] =
        int(luaL_checkinteger(L, 3));
    return 0;
}
static int LuaAnimatorGetInt(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushinteger(L, 0); return 1; }
    auto it = an->paramInts.find(luaL_checkstring(L, 2));
    lua_pushinteger(L,
        it != an->paramInts.end() ? it->second : 0);
    return 1;
}
static int LuaAnimatorSetTrigger(lua_State* L) {
    // Pulses the trigger -- the M5c10 controller runtime
    // auto-resets it to false the moment the firing transition
    // consumes it.
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramBools[luaL_checkstring(L, 2)] = true;
    return 0;
}
static int LuaAnimatorSetVec2(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    NaraGame::Vec2 v{
        float(luaL_checknumber(L, 3)),
        float(luaL_checknumber(L, 4))};
    an->paramVec2[luaL_checkstring(L, 2)] = v;
    return 0;
}
static int LuaAnimatorGetVec2(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0); return 2; }
    auto it = an->paramVec2.find(luaL_checkstring(L, 2));
    if (it == an->paramVec2.end()) {
        lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0);
    } else {
        lua_pushnumber(L, it->second.x);
        lua_pushnumber(L, it->second.y);
    }
    return 2;
}
static int LuaAnimatorSetColor(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    NaraGame::Color c{
        float(luaL_checknumber(L, 3)),
        float(luaL_checknumber(L, 4)),
        float(luaL_checknumber(L, 5)),
        float(luaL_checknumber(L, 6))};
    an->paramColors[luaL_checkstring(L, 2)] = c;
    return 0;
}
static int LuaAnimatorGetColor(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) {
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
        return 4;
    }
    auto it = an->paramColors.find(luaL_checkstring(L, 2));
    if (it == an->paramColors.end()) {
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
    } else {
        lua_pushnumber(L, it->second.r);
        lua_pushnumber(L, it->second.g);
        lua_pushnumber(L, it->second.b);
        lua_pushnumber(L, it->second.a);
    }
    return 4;
}
static int LuaAnimatorSetString(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) return 0;
    an->paramStrings[luaL_checkstring(L, 2)] =
        luaL_checkstring(L, 3);
    return 0;
}
static int LuaAnimatorGetString(lua_State* L) {
    auto* an = FindAnimator(HostFromUpvalue(L), luaL_checkstring(L, 1));
    if (!an) { lua_pushstring(L, ""); return 1; }
    auto it = an->paramStrings.find(luaL_checkstring(L, 2));
    lua_pushstring(L,
        it != an->paramStrings.end() ? it->second.c_str() : "");
    return 1;
}

// animator.current_state(entity) -> string. Returns the name of
// the AnimatorController's currently-active state, or "" when
// the entity has no animator / no controller / a controller-
// less Animator that just plays a single clip. Lets scripts
// react to state-machine transitions ("did I just enter the
// Hit state?") without rebuilding the controller graph in Lua.
static int LuaAnimatorCurrentState(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    auto* an = FindAnimator(host, luaL_checkstring(L, 1));
    if (!an || an->currentState < 0 || !an->controller.IsValid()) {
        lua_pushstring(L, ""); return 1;
    }
    AssetDatabase* db = host ? host->BoundAssetDatabase() : nullptr;
    if (!db) { lua_pushstring(L, ""); return 1; }
    AnimatorControllerAsset* ctrl = db->GetAnimatorController(an->controller);
    if (!ctrl || an->currentState >= int(ctrl->states.size())) {
        lua_pushstring(L, ""); return 1;
    }
    lua_pushstring(L, ctrl->states[an->currentState].name.c_str());
    return 1;
}

// animator.in_state(entity, name) -> bool. Convenience over
// current_state for the common "are we still in this state?"
// branch.
static int LuaAnimatorInState(lua_State* L) {
    ScriptHost* host = HostFromUpvalue(L);
    auto* an = FindAnimator(host, luaL_checkstring(L, 1));
    const char* want = luaL_checkstring(L, 2);
    if (!an || an->currentState < 0 || !an->controller.IsValid()) {
        lua_pushboolean(L, 0); return 1;
    }
    AssetDatabase* db = host ? host->BoundAssetDatabase() : nullptr;
    if (!db) { lua_pushboolean(L, 0); return 1; }
    AnimatorControllerAsset* ctrl = db->GetAnimatorController(an->controller);
    if (!ctrl || an->currentState >= int(ctrl->states.size())) {
        lua_pushboolean(L, 0); return 1;
    }
    lua_pushboolean(L,
        ctrl->states[an->currentState].name == want ? 1 : 0);
    return 1;
}

// `print(...)` in script land lands here. Concatenates the args
// like the stock Lua print does + pushes the joined line into the
// host's output log.
static int LuaPrintRedirect(lua_State* L) {
    ScriptHost* host = static_cast<ScriptHost*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!host) return 0;
    const int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) line += '\t';
        line.append(s, len);
        lua_pop(L, 1);
    }
    host->AppendLine(line);
    return 0;
}

// Game-dev math helpers that extend Lua's stdlib `math` table.
// Authored as pure Lua so the bootstrap doubles as documentation +
// new helpers cost zero C++ surface. User scripts can override
// any of these by assigning a new function back into `math.*`.
static const char* kMathHelpers =
    "function math.lerp(a, b, t) return a + (b - a) * t end\n"
    "function math.clamp(x, lo, hi)\n"
    "  if x < lo then return lo end\n"
    "  if x > hi then return hi end\n"
    "  return x\n"
    "end\n"
    "function math.sign(x)\n"
    "  if x > 0 then return 1 end\n"
    "  if x < 0 then return -1 end\n"
    "  return 0\n"
    "end\n"
    "function math.smoothstep(e0, e1, x)\n"
    "  if e0 == e1 then return 0 end\n"
    "  local t = math.clamp((x - e0) / (e1 - e0), 0, 1)\n"
    "  return t * t * (3 - 2 * t)\n"
    "end\n"
    "function math.distance(x1, y1, x2, y2)\n"
    "  local dx, dy = x2 - x1, y2 - y1\n"
    "  return math.sqrt(dx * dx + dy * dy)\n"
    "end\n"
    "function math.length(x, y)\n"
    "  return math.sqrt(x * x + y * y)\n"
    "end\n"
    "function math.normalize(x, y)\n"
    "  local len = math.sqrt(x * x + y * y)\n"
    "  if len < 1e-6 then return 0, 0 end\n"
    "  return x / len, y / len\n"
    "end\n"
    "function math.dot(x1, y1, x2, y2)\n"
    "  return x1 * x2 + y1 * y2\n"
    "end\n";

// Default OnAnimationEvent handler. Mirrors the M6c1 stub format so
// the Console pane reads the same with or without a vendored Lua;
// user scripts can later overwrite this global with their own
// implementation.
static const char* kDefaultBootstrap =
    "function OnAnimationEvent(entity, anim, event,\n"
    "                          s, f, i, b,\n"
    "                          vx, vy, r, g, bl, a)\n"
    "    local has_args = (s ~= '' or f ~= 0 or i ~= 0 or b\n"
    "                      or vx ~= 0 or vy ~= 0 or a ~= 0)\n"
    "    if has_args then\n"
    "        print(string.format(\n"
    "            '[lua] %s.%s.%s(\\\"%s\\\", %.3f, %d, %s, "
                "(%.2f,%.2f), (%.2f,%.2f,%.2f,%.2f))',\n"
    "            entity, anim, event, s, f, i, tostring(b),\n"
    "            vx, vy, r, g, bl, a))\n"
    "    else\n"
    "        print(string.format('[lua] %s.%s.%s()',\n"
    "            entity, anim, event))\n"
    "    end\n"
    "end\n";

ScriptHost::ScriptHost() {
    lua_State* L = luaL_newstate();
    if (!L) return;
    m_lua = L;
    luaL_openlibs(L);
    // Replace print with our log-redirector. The host pointer
    // travels as an upvalue so the C callback can find us.
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, LuaPrintRedirect, 1);
    lua_setglobal(L, "print");
    // Register the `animator` table. Every callback shares the
    // same `this` pointer as a closure upvalue so the C side can
    // find its way back to the bound scene without globals.
    static const luaL_Reg kAnimatorApi[] = {
        {"set_bool",    LuaAnimatorSetBool},
        {"get_bool",    LuaAnimatorGetBool},
        {"set_float",   LuaAnimatorSetFloat},
        {"get_float",   LuaAnimatorGetFloat},
        {"set_int",     LuaAnimatorSetInt},
        {"get_int",     LuaAnimatorGetInt},
        {"set_trigger", LuaAnimatorSetTrigger},
        {"set_vec2",    LuaAnimatorSetVec2},
        {"get_vec2",    LuaAnimatorGetVec2},
        {"set_color",   LuaAnimatorSetColor},
        {"get_color",   LuaAnimatorGetColor},
        {"set_string",    LuaAnimatorSetString},
        {"get_string",    LuaAnimatorGetString},
        {"current_state", LuaAnimatorCurrentState},
        {"in_state",      LuaAnimatorInState},
        {nullptr,       nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kAnimatorApi, 1);
    lua_setglobal(L, "animator");
    static const luaL_Reg kTransformApi[] = {
        {"set_position", LuaTransformSetPosition},
        {"get_position", LuaTransformGetPosition},
        {"set_rotation", LuaTransformSetRotation},
        {"get_rotation", LuaTransformGetRotation},
        {"set_scale",    LuaTransformSetScale},
        {"get_scale",    LuaTransformGetScale},
        {"translate",    LuaTransformTranslate},
        {nullptr,        nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kTransformApi, 1);
    lua_setglobal(L, "transform");
    static const luaL_Reg kRigidbodyApi[] = {
        {"set_velocity", LuaRigidbodySetVelocity},
        {"get_velocity", LuaRigidbodyGetVelocity},
        {"add_force",    LuaRigidbodyAddForce},
        {"set_gravity",  LuaRigidbodySetGravity},
        {"get_gravity",  LuaRigidbodyGetGravity},
        {"set_drag",     LuaRigidbodySetDrag},
        {"get_drag",     LuaRigidbodyGetDrag},
        {nullptr,        nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kRigidbodyApi, 1);
    lua_setglobal(L, "rigidbody");
    static const luaL_Reg kSpriteApi[] = {
        {"set_color",      LuaSpriteSetColor},
        {"get_color",      LuaSpriteGetColor},
        {"set_alpha",      LuaSpriteSetAlpha},
        {"get_alpha",      LuaSpriteGetAlpha},
        {"set_sub_sprite", LuaSpriteSetSubSprite},
        {"get_sub_sprite", LuaSpriteGetSubSprite},
        {"set_flip_x",     LuaSpriteSetFlipX},
        {"get_flip_x",     LuaSpriteGetFlipX},
        {"set_flip_y",     LuaSpriteSetFlipY},
        {"get_flip_y",     LuaSpriteGetFlipY},
        {"set_size",       LuaSpriteSetSize},
        {"get_size",       LuaSpriteGetSize},
        {"set_order",      LuaSpriteSetOrder},
        {"get_order",      LuaSpriteGetOrder},
        {nullptr,          nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kSpriteApi, 1);
    lua_setglobal(L, "sprite");
    static const luaL_Reg kInputApi[] = {
        {"is_key_down",       LuaInputIsKeyDown},
        {"is_key_pressed",    LuaInputIsKeyPressed},
        {"is_key_released",   LuaInputIsKeyReleased},
        {"mouse_position",    LuaInputMousePosition},
        {"is_mouse_down",     LuaInputIsMouseDown},
        {"is_mouse_pressed",  LuaInputIsMousePressed},
        {"is_mouse_released", LuaInputIsMouseReleased},
        {"mouse_wheel",       LuaInputMouseWheel},
        {nullptr,             nullptr},
    };
    lua_newtable(L);
    // No upvalue needed -- ImGui's input state lives in a thread-
    // local context the callbacks reach through ImGui::*. We pass
    // a zero closure count so luaL_setfuncs doesn't try to pop a
    // missing upvalue from the stack.
    luaL_setfuncs(L, kInputApi, 0);
    lua_setglobal(L, "input");
    static const luaL_Reg kTimeApi[] = {
        {"delta",       LuaTimeDelta},
        {"elapsed",     LuaTimeElapsed},
        {"frame_count", LuaTimeFrameCount},
        {"fps",         LuaTimeFps},
        {nullptr,       nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kTimeApi, 1);
    lua_setglobal(L, "time");
    // M8c1 audio table -- single-voice PlaySound MVP. The
    // closure carries `this` so the binding can resolve
    // project-relative paths through the asset DB without
    // bouncing through a global.
    static const luaL_Reg kAudioApi[] = {
        {"play",              LuaAudioPlay},
        {"stop",              LuaAudioStop},
        {"play_music",        LuaAudioPlayMusic},
        {"stop_music",        LuaAudioStopMusic},
        {"set_bus_volume",    LuaAudioSetBusVolume},
        {"get_bus_volume",    LuaAudioGetBusVolume},
        {"spawn",             LuaAudioSpawn},
        {"stop_voice",        LuaAudioStopVoice},
        {"pause_voice",       LuaAudioPauseVoice},
        {"resume_voice",      LuaAudioResumeVoice},
        {"set_voice_volume",  LuaAudioSetVoiceVolume},
        {"is_voice_playing",  LuaAudioIsVoicePlaying},
        {"play_positional",   LuaAudioPlayPositional},
        {"set_reverb_wet",    LuaAudioSetReverbWet},
        {"get_reverb_wet",    LuaAudioGetReverbWet},
        {nullptr,             nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kAudioApi, 1);
    lua_setglobal(L, "audio");
    // M9c3 particle table. Same upvalue idiom as audio --
    // every binding takes the target entity name as its
    // first argument + resolves through ScriptHost::BoundScene.
    static const luaL_Reg kParticleApi[] = {
        {"emit",               LuaParticleEmit},
        {"clear",              LuaParticleClear},
        {"set_emission_rate",  LuaParticleSetEmissionRate},
        {"get_count",          LuaParticleGetCount},
        {"set_playing",        LuaParticleSetPlaying},
        {"is_playing",         LuaParticleIsPlaying},
        {nullptr,              nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kParticleApi, 1);
    lua_setglobal(L, "particle");
    // M9c11 camera table. Same upvalue idiom; every binding
    // resolves the named entity through ScriptHost::BoundScene.
    static const luaL_Reg kCameraApi[] = {
        {"set_zoom",     LuaCameraSetZoom},
        {"get_zoom",     LuaCameraGetZoom},
        {"set_priority", LuaCameraSetPriority},
        {"get_priority", LuaCameraGetPriority},
        {"get_active",   LuaCameraGetActive},
        {nullptr,        nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kCameraApi, 1);
    lua_setglobal(L, "camera");
    static const luaL_Reg kSceneApi[] = {
        {"create_entity",     LuaSceneCreateEntity},
        {"destroy_entity",    LuaSceneDestroyEntity},
        {"find",              LuaSceneFind},
        {"entity_count",      LuaSceneEntityCount},
        {"all_entities",      LuaSceneAllEntities},
        {"add_component",     LuaSceneAddComponent},
        {"has_component",     LuaSceneHasComponent},
        {"remove_component",  LuaSceneRemoveComponent},
        {"set_enabled",       LuaSceneSetEnabled},
        {"is_enabled",        LuaSceneIsEnabled},
        {"parent_of",         LuaSceneParentOf},
        {"children_of",       LuaSceneChildrenOf},
        {"set_parent",        LuaSceneSetParent},
        {"destroy_self",      LuaSceneDestroySelf},
        {"entity_at",         LuaSceneEntityAt},
        {"find_by_component", LuaSceneFindByComponent},
        {"broadcast",         LuaSceneBroadcast},
        {"raycast",           LuaSceneRaycast},
        {"overlap_box",       LuaSceneOverlapBox},
        {"overlap_circle",    LuaSceneOverlapCircle},
        {"find_path",         LuaSceneFindPath},
        {"load",              LuaSceneLoad},
        {nullptr,             nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    luaL_setfuncs(L, kSceneApi, 1);
    lua_setglobal(L, "scene");
    if (luaL_dostring(L, kDefaultBootstrap) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine(std::string("[lua-error] bootstrap: ") +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    if (luaL_dostring(L, kMathHelpers) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine(std::string("[lua-error] math helpers: ") +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

// Tries the `<entryFunction>_<suffix>` global first when an entry
// function is set, then falls back to the bare `On<suffix>`
// global. Returns the actual name placed on top of the Lua stack
// (caller is responsible for popping or pcall'ing); returns empty
// when neither candidate is a function (caller must pop the
// trailing nil itself).
static std::string PushLifecycleHook(lua_State* L,
                                     const std::string& entryFunction,
                                     const char* suffix) {
    if (!entryFunction.empty()) {
        const std::string entryName = entryFunction + "_" + suffix;
        lua_getglobal(L, entryName.c_str());
        if (lua_isfunction(L, -1)) return entryName;
        lua_pop(L, 1);
    }
    const std::string fallback = std::string("On") + suffix;
    lua_getglobal(L, fallback.c_str());
    if (lua_isfunction(L, -1)) return fallback;
    return std::string{};
}

void ScriptHost::CallOnStart(const std::string& entityName,
                             const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "Start");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

void ScriptHost::CallOnUpdate(const std::string& entityName,
                              float dt,
                              const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "Update");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    lua_pushnumber(L, dt);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

void ScriptHost::CallOnDestroy(const std::string& entityName,
                               const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "Destroy");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

void ScriptHost::CallOnEnable(const std::string& entityName,
                              const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "Enable");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

void ScriptHost::CallOnDisable(const std::string& entityName,
                               const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "Disable");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

void ScriptHost::CallOnTriggerEnter(const std::string& entityName,
                                    const std::string& otherName,
                                    const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "TriggerEnter");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    lua_pushstring(L, otherName.c_str());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

void ScriptHost::CallOnTriggerStay(const std::string& entityName,
                                   const std::string& otherName,
                                   const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "TriggerStay");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    lua_pushstring(L, otherName.c_str());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

void ScriptHost::CallOnTriggerExit(const std::string& entityName,
                                   const std::string& otherName,
                                   const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "TriggerExit");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    lua_pushstring(L, otherName.c_str());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

// Helper shared by the three Collision dispatchers -- same body
// as the Trigger trio with a configurable suffix.
namespace {
void DispatchTwoArgHook(void* luaState, std::string* dispatchEntity,
                        std::vector<std::string>* outputLog,
                        const std::string& entityName,
                        const std::string& otherName,
                        const std::string& entryFunction,
                        const char* suffix) {
    if (!luaState) return;
    lua_State* L = static_cast<lua_State*>(luaState);
    std::string fn = PushLifecycleHook(L, entryFunction, suffix);
    if (fn.empty()) { lua_pop(L, 1); return; }
    *dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    lua_pushstring(L, otherName.c_str());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        outputLog->push_back("[lua-error] " + fn + ": " +
                             (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    dispatchEntity->clear();
}
} // namespace

void ScriptHost::CallOnCollisionEnter(const std::string& entityName,
                                      const std::string& otherName,
                                      const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "CollisionEnter");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    lua_pushstring(L, otherName.c_str());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

void ScriptHost::CallOnCollisionStay(const std::string& entityName,
                                     const std::string& otherName,
                                     const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "CollisionStay");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    lua_pushstring(L, otherName.c_str());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

void ScriptHost::CallOnCollisionExit(const std::string& entityName,
                                     const std::string& otherName,
                                     const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    std::string fn = PushLifecycleHook(L, entryFunction, "CollisionExit");
    if (fn.empty()) { lua_pop(L, 1); return; }
    m_dispatchEntity = entityName;
    lua_pushstring(L, entityName.c_str());
    lua_pushstring(L, otherName.c_str());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + fn + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

bool ScriptHost::LoadFile(const std::string& absolutePath,
                          const std::string& displayName) {
    if (!m_lua) return false;
    lua_State* L = static_cast<lua_State*>(m_lua);
    if (luaL_loadfile(L, absolutePath.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + displayName + " load: " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + displayName + " run: " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
        return false;
    }
    AppendLine("[lua] loaded " + displayName);
    return true;
}

bool ScriptHost::LoadString(const std::string& code,
                            const std::string& displayName) {
    if (!m_lua) return false;
    lua_State* L = static_cast<lua_State*>(m_lua);
    if (luaL_loadbuffer(L, code.c_str(), code.size(),
                         displayName.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + displayName + " load: " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + displayName + " run: " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
        return false;
    }
    AppendLine("[lua] loaded " + displayName);
    return true;
}

ScriptHost::~ScriptHost() {
    if (m_lua) {
        lua_close(static_cast<lua_State*>(m_lua));
        m_lua = nullptr;
    }
}

void ScriptHost::OnAnimationEvent(
    const std::string& entityName,
    const std::string& animationName,
    const AnimationEvent& event,
    const std::string& entryFunction) {
    if (!m_lua) return;
    lua_State* L = static_cast<lua_State*>(m_lua);
    // Prefer the per-entity entry function. If it isn't a valid
    // function fall back to the global OnAnimationEvent so the
    // bootstrap still catches the dispatch.
    std::string usedName;
    if (!entryFunction.empty()) {
        lua_getglobal(L, entryFunction.c_str());
        if (lua_isfunction(L, -1)) {
            usedName = entryFunction;
        } else {
            lua_pop(L, 1);
        }
    }
    if (usedName.empty()) {
        lua_getglobal(L, "OnAnimationEvent");
        if (!lua_isfunction(L, -1)) {
            // Tolerate a user script that nuked the global by
            // quietly dropping the dispatch.
            lua_pop(L, 1);
            return;
        }
        usedName = "OnAnimationEvent";
    }
    m_dispatchEntity = entityName;
    lua_pushstring (L, entityName.c_str());
    lua_pushstring (L, animationName.c_str());
    lua_pushstring (L, event.name.c_str());
    lua_pushstring (L, event.stringArg.c_str());
    lua_pushnumber (L, event.floatArg);
    lua_pushinteger(L, event.intArg);
    lua_pushboolean(L, event.boolArg ? 1 : 0);
    lua_pushnumber (L, event.vec2Arg.x);
    lua_pushnumber (L, event.vec2Arg.y);
    lua_pushnumber (L, event.colorArg.r);
    lua_pushnumber (L, event.colorArg.g);
    lua_pushnumber (L, event.colorArg.b);
    lua_pushnumber (L, event.colorArg.a);
    if (lua_pcall(L, 13, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AppendLine("[lua-error] " + usedName + ": " +
                   (err ? err : "<no message>"));
        lua_pop(L, 1);
    }
    m_dispatchEntity.clear();
}

#else
// -----------------------------------------------------------------
// Stub fallback (M6c1). Used when third_party/lua isn't vendored.
// Formats the dispatch as a `[script]` "function call" string so
// the Console pane still shows something meaningful.
// -----------------------------------------------------------------

ScriptHost::ScriptHost()  = default;
ScriptHost::~ScriptHost() = default;

void ScriptHost::CallOnStart(const std::string&,
                             const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnUpdate(const std::string&, float,
                              const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnDestroy(const std::string&,
                               const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnEnable(const std::string&,
                              const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnDisable(const std::string&,
                               const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnTriggerEnter(const std::string&, const std::string&,
                                    const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnTriggerStay (const std::string&, const std::string&,
                                    const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnTriggerExit (const std::string&, const std::string&,
                                    const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnCollisionEnter(const std::string&, const std::string&,
                                      const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnCollisionStay (const std::string&, const std::string&,
                                      const std::string&) { /* stub no-op */ }
void ScriptHost::CallOnCollisionExit (const std::string&, const std::string&,
                                      const std::string&) { /* stub no-op */ }

bool ScriptHost::LoadFile(const std::string&,
                          const std::string& displayName) {
    AppendLine("[script-stub] would load " + displayName +
               " (vendor Lua to enable)");
    return false;
}

bool ScriptHost::LoadString(const std::string&,
                            const std::string& displayName) {
    AppendLine("[script-stub] would load " + displayName +
               " (vendor Lua to enable)");
    return false;
}

void ScriptHost::OnAnimationEvent(
    const std::string& entityName,
    const std::string& animationName,
    const AnimationEvent& event,
    const std::string& entryFunction) {
    // Stub path: include the entry function in the prefix so the
    // editor still tells the user which fn would have been called
    // when Lua is eventually wired in.
    const std::string fn = entryFunction.empty()
        ? "OnAnimationEvent" : entryFunction;
    const bool hasArgs = !event.stringArg.empty() ||
                         event.floatArg != 0.0f  ||
                         event.intArg   != 0     ||
                         event.boolArg ||
                         event.vec2Arg.x != 0.0f || event.vec2Arg.y != 0.0f ||
                         event.colorArg.a != 0.0f;
    char buf[512];
    if (hasArgs) {
        std::snprintf(buf, sizeof(buf),
                      "[script:%s] %s.%s.%s(\"%s\", %.3f, %d, %s, "
                      "(%.2f,%.2f), (%.2f,%.2f,%.2f,%.2f))",
                      fn.c_str(),
                      entityName.c_str(),
                      animationName.c_str(),
                      event.name.c_str(),
                      event.stringArg.c_str(),
                      double(event.floatArg),
                      event.intArg,
                      event.boolArg ? "true" : "false",
                      double(event.vec2Arg.x),
                      double(event.vec2Arg.y),
                      double(event.colorArg.r),
                      double(event.colorArg.g),
                      double(event.colorArg.b),
                      double(event.colorArg.a));
    } else {
        std::snprintf(buf, sizeof(buf),
                      "[script:%s] %s.%s.%s()",
                      fn.c_str(),
                      entityName.c_str(),
                      animationName.c_str(),
                      event.name.c_str());
    }
    AppendLine(buf);
}

#endif

} // namespace SceneEditor
