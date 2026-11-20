#include "rm_engine/particles_x_physics/runtime/Register.h"

#include "core/reflect/Reflect.h"
#include "rm_engine/game_x_particles/runtime/CollisionShapes.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/physics/runtime/Shapes.h"
#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace rm::particles_x_physics {

namespace {

using particles::CollisionShape;
using particles::CollisionType;
using space3d::Mat4;

void Put(float (&to)[3], Vec3 v) {
    to[0] = v.x;
    to[1] = v.y;
    to[2] = v.z;
}

// 回転の 1 列 (m[列][行]): エンティティの軸の 1 つをワールドで表したもの (長さ 1)。
Vec3 Column(const Mat4& m, int c) { return {m.m[c][0], m.m[c][1], m.m[c][2]}; }

}  // namespace

void GatherShapes(const game_production::Scene& scene, const std::function<const Mat4*(int)>& world, std::vector<CollisionShape>& out) {
    // 物理が物体を作る時と同じ形 (physics::DimsOf / PoseOf: 同じ大きさ、同じ最小値、キャラクターは
    // 立たせて回さず拡縮もしない - そこに 1 度だけ書いてある、2026-11-19)。
    const std::string collider = reflect::TypeOf<physics::Collider>().name, character = reflect::TypeOf<physics::Character>().name;
    auto add = [&](const physics::ShapeDims& d, const physics::Pose& pose) {
        CollisionShape s{};
        Put(s.axisX, Column(pose.turn, 0));
        Put(s.axisY, Column(pose.turn, 1));
        Put(s.axisZ, Column(pose.turn, 2));
        const Vec3 at = space3d::TransformDirection(pose.turn, d.offset);
        Put(s.center, {pose.position.x + at.x, pose.position.y + at.y, pose.position.z + at.z});
        switch (d.kind) {
            case physics::ShapeDims::Kind::Box:
                s.type = CollisionType::Box;
                s.sizeX = d.half.x;
                s.sizeY = d.half.y;
                s.sizeZ = d.half.z;
                break;
            case physics::ShapeDims::Kind::Sphere:
                s.type = CollisionType::Sphere;
                s.sizeX = d.radius;
                break;
            case physics::ShapeDims::Kind::Capsule:
                s.type = CollisionType::Capsule;
                s.sizeX = d.radius;
                s.sizeY = d.halfHeight;
                break;
            case physics::ShapeDims::Kind::Mesh: return;  // (メッシュの形はモデルのもの: ここでは扱わない)
        }
        out.push_back(s);
    };
    for (const game_production::SceneEntity& e : scene.Entities()) {
        const Mat4* w = world ? world(e.data.id) : nullptr;
        if (!w) continue;
        const physics::Pose pose = physics::PoseOf(*w);
        for (const game_production::Component& comp : e.components) {
            if (!comp.Known()) continue;
            if (comp.type == collider) {
                const auto& c = *static_cast<const physics::Collider*>(comp.value.Get());
                if (!c.trigger) add(physics::DimsOf(c, pose.scale), pose);
            } else if (comp.type == character) {
                physics::Pose upright;
                upright.position = pose.position;
                add(physics::DimsOf(*static_cast<const physics::Character*>(comp.value.Get())), upright);
            }
        }
    }
}

void RegisterRuntime(RuntimeRegistry& registry) {
    static const game_x_particles::CollisionShapesInfo kShapes{&GatherShapes};
    registry.Contribute(game_x_particles::kCollisionShapesPoint, kShapes);
}

}  // namespace rm::particles_x_physics

RM_RUNTIME_ENTRY(rm_engine, particles_x_physics, rm::particles_x_physics::RegisterRuntime)
