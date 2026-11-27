#include "rm_engine/navigation_x_physics/runtime/Register.h"

#include "base/Log.h"
#include "rm_engine/game_x_navigation/runtime/NavScene.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/physics/runtime/PhysicsWorld.h"
#include "rm_engine/physics/runtime/Shapes.h"
#include "rm_engine/space3d/runtime/MeshBake.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/Primitives.h"

#include <map>
#include <optional>
#include <string>

namespace rm::navigation_x_physics {

namespace {

using game_production::Component;
using game_production::SceneEntity;
using game_x_navigation::NavShape;

template <class T>
const T* Find(const SceneEntity& e) {
    const Component* c = e.FindComponent(reflect::TypeOf<T>().name);
    return c && c->Known() ? static_cast<const T*>(c->value.Get()) : nullptr;
}

std::vector<Vec3> Scaled(const space3d::MeshData& m, Vec3 s) {
    std::vector<Vec3> out;
    out.reserve(m.vertices.size());
    for (const space3d::MeshVertex& v : m.vertices) out.push_back({v.position.x * s.x, v.position.y * s.y, v.position.z * s.z});
    return out;
}

void ColliderPrint(const game_production::Scene& scene, const game_x_navigation::Worlds& worlds, const game_production::AssetAccess*, navigation::Hasher& h) {
    const std::string collider = reflect::TypeOf<physics::Collider>().name, body = reflect::TypeOf<physics::Rigidbody>().name,
                      character = reflect::TypeOf<physics::Character>().name;
    for (const SceneEntity& e : scene.Entities()) {
        if (!e.FindComponent(collider) && !e.FindComponent(body) && !e.FindComponent(character)) continue;
        // (コライダーの形が「メッシュ」なら、同じエンティティのメッシュの部品も形になる: 全部の部品を)
        for (const Component& c : e.components) game_x_navigation::HashComponent(h, e, c, worlds);
    }
}

bool OwnsCharacter(game_production::Scene& scene, int entity) {
    const SceneEntity* e = scene.Find(entity);
    return e && Find<physics::Character>(*e);
}

void MoveCharacter(game_production::Scene& scene, int entity, Vec3 velocity) { physics::WorldOf(scene).Move(scene, entity, velocity); }

bool CharacterBody(const game_production::Scene& scene, int entity, game_x_navigation::NavBody& out) {
    const SceneEntity* e = scene.Find(entity);
    const physics::Character* c = e ? Find<physics::Character>(*e) : nullptr;
    if (!c) return false;
    out.radius = c->radius, out.height = c->height, out.maxSlope = c->maxSlope, out.stepHeight = c->stepHeight;
    return true;
}

}  // namespace

bool Moves(const SceneEntity& e) {
    if (Find<physics::Character>(e)) return true;
    const physics::Rigidbody* b = Find<physics::Rigidbody>(e);
    return b && b->motion != physics::Motion::Static;
}

void CollectColliders(const game_x_navigation::NavSourceContext& ctx, std::vector<NavShape>& out) {
    std::map<int, space3d::MeshData> primitives;
    std::map<std::string, std::optional<space3d::MeshData>> models;
    auto shape = [&](space3d::Shape s) -> const space3d::MeshData& {
        auto [it, fresh] = primitives.try_emplace(static_cast<int>(s));
        if (fresh) it->second = space3d::ShapeMesh(s);
        return it->second;
    };
    for (const SceneEntity& e : ctx.scene.Entities()) {
        if (ctx.stop.stop_requested()) return;
        const physics::Collider* c = Find<physics::Collider>(e);
        if (!c || c->trigger || Moves(e)) continue;
        const auto w = ctx.worlds.find(e.data.id);
        const physics::Pose pose = physics::PoseOf(w != ctx.worlds.end() ? w->second : space3d::Mat4{});
        const physics::ShapeDims d = physics::DimsOf(*c, pose.scale);
        // 形の空間 (大きさを掛けた点) -> ワールド: 回して、ずらして、置く。
        space3d::Mat4 place = pose.turn;
        const Vec3 off = space3d::TransformPoint(pose.turn, d.offset);
        place.m[3][0] = pose.position.x + off.x, place.m[3][1] = pose.position.y + off.y, place.m[3][2] = pose.position.z + off.z;
        const space3d::MeshData* mesh = nullptr;
        Vec3 scale{1, 1, 1};
        using Kind = physics::ShapeDims::Kind;
        switch (d.kind) {
            case Kind::Box: mesh = &shape(space3d::Shape::Box), scale = {d.half.x * 2, d.half.y * 2, d.half.z * 2}; break;
            case Kind::Sphere: mesh = &shape(space3d::Shape::Sphere), scale = {d.radius * 2, d.radius * 2, d.radius * 2}; break;
            // (カプセルの形は直径 1・高さ 2: 横を半径に、縦を端から端に合わせる。丸い端は少しゆがむ)
            case Kind::Capsule: mesh = &shape(space3d::Shape::Capsule), scale = {d.radius * 2, d.halfHeight + d.radius, d.radius * 2}; break;
            case Kind::Cylinder: mesh = &shape(space3d::Shape::Cylinder), scale = {d.radius * 2, d.halfHeight * 2, d.radius * 2}; break;
            case Kind::Cone: mesh = &shape(space3d::Shape::Cone), scale = {d.radius * 2, d.halfHeight * 2, d.radius * 2}; break;
            case Kind::Pyramid: mesh = &shape(space3d::Shape::Pyramid), scale = {d.half.x * 2, d.half.y * 2, d.half.z * 2}; break;
            case Kind::Mesh: {
                const physics::MeshSource src = physics::MeshOf(e);
                scale = d.meshScale;
                if (src.primitive) {
                    mesh = &shape(*src.primitive);
                } else if (!src.model.Empty()) {
                    auto [it, fresh] = models.try_emplace(src.model.guid);
                    if (fresh) {
                        std::string err;
                        if (ctx.assets && ctx.assets->file) {
                            if (const auto file = ctx.assets->file(src.model); !file.empty()) it->second = space3d::LoadMeshFile(file, &err);
                        }
                        if (!it->second && ctx.assets && ctx.assets->bytes) {
                            if (const auto bytes = ctx.assets->bytes(src.model)) it->second = space3d::ReadBakedMesh(*bytes, &err);
                        }
                        if (!it->second) log::Warn("ナビ: メッシュのコライダーのモデルを読めません ({}): {}", e.data.name, err.empty() ? std::string("モデルがありません") : err);
                    }
                    if (it->second) mesh = &*it->second;
                }
                break;
            }
        }
        if (!mesh || mesh->indices.size() < 3) continue;
        NavShape s;
        s.entity = e.data.id;
        game_x_navigation::AddMeshTo(s, Scaled(*mesh, scale), mesh->indices, place);
        out.push_back(std::move(s));
    }
}

void RegisterRuntime(RuntimeRegistry& registry) {
    // 焼く物「動かないコライダー」: 部品「ナビメッシュ」の「コライダーを焼く」で切り替える (欄の名前 colliders)。
    static const game_x_navigation::NavSourceInfo kColliders{"colliders", "動かないコライダー", "3d", &CollectColliders, &ColliderPrint, &Moves};
    registry.Contribute(game_x_navigation::kNavSourcePoint, kColliders);
    // 動く者を動かす道: キャラクター (坂・段差・押されるのが物理どおり)。向きはナビが回す (キャラクターは回転を持たない)。
    static const game_x_navigation::NavMoverInfo kMover{&OwnsCharacter, &MoveCharacter, 0, false, "「{}」はキャラクターで歩きます"};
    registry.Contribute(game_x_navigation::kNavMoverPoint, kMover);
    // 体の形: キャラクターの値 (型と違えば、インスペクターに印と「キャラクターに合わせる」)。
    static const game_x_navigation::NavBodyInfo kBody{"キャラクター", &CharacterBody};
    registry.Contribute(game_x_navigation::kNavBodyPoint, kBody);
}

}  // namespace rm::navigation_x_physics

RM_RUNTIME_ENTRY(rm_engine, navigation_x_physics, rm::navigation_x_physics::RegisterRuntime)
