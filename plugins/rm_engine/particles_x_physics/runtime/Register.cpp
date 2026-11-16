#include "rm_engine/particles_x_physics/runtime/Register.h"

#include "core/reflect/Reflect.h"
#include "rm_engine/game_x_particles/runtime/CollisionShapes.h"
#include "rm_engine/physics/runtime/Components.h"

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

// A column of the matrix (m[column][row]): an axis of the entity in the world, with its scale.
Vec3 Column(const Mat4& m, int c) { return {m.m[c][0], m.m[c][1], m.m[c][2]}; }
float Length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 Unit(Vec3 v, Vec3 otherwise) {
    const float l = Length(v);
    return l > 1e-8f ? Vec3{v.x / l, v.y / l, v.z / l} : otherwise;
}

}  // namespace

void GatherShapes(const game_production::Scene& scene, const std::function<const Mat4*(int)>& world, std::vector<CollisionShape>& out) {
    const std::string collider = reflect::TypeOf<physics::Collider>().name, character = reflect::TypeOf<physics::Character>().name;
    for (const game_production::SceneEntity& e : scene.Entities()) {
        const Mat4* w = world ? world(e.data.id) : nullptr;
        if (!w) continue;
        const Vec3 ax = Column(*w, 0), ay = Column(*w, 1), az = Column(*w, 2);
        const float sx = Length(ax), sy = Length(ay), sz = Length(az);
        const Vec3 ux = Unit(ax, {1, 0, 0}), uy = Unit(ay, {0, 1, 0}), uz = Unit(az, {0, 0, 1});
        for (const game_production::Component& comp : e.components) {
            if (!comp.Known()) continue;
            CollisionShape s{};
            Put(s.axisX, ux);
            Put(s.axisY, uy);
            Put(s.axisZ, uz);
            if (comp.type == collider) {
                const auto& c = *static_cast<const physics::Collider*>(comp.value.Get());
                if (c.trigger || c.shape == physics::Shape::Mesh) continue;  // (a mesh's shape is the model's: not here)
                Put(s.center, space3d::TransformPoint(*w, c.offset));
                switch (c.shape) {
                    case physics::Shape::Box:
                        s.type = CollisionType::Box;
                        s.sizeX = c.size.x * sx * 0.5f;
                        s.sizeY = c.size.y * sy * 0.5f;
                        s.sizeZ = c.size.z * sz * 0.5f;
                        break;
                    case physics::Shape::Sphere:
                        s.type = CollisionType::Sphere;
                        s.sizeX = c.radius * std::max({sx, sy, sz});
                        break;
                    case physics::Shape::Capsule:
                        s.type = CollisionType::Capsule;
                        s.sizeX = c.radius * std::max(sx, sz);
                        s.sizeY = std::max(0.f, c.height * sy * 0.5f - s.sizeX);
                        break;
                    default: continue;
                }
                out.push_back(s);
            } else if (comp.type == character) {
                // The body: a capsule standing on the entity's place.
                const auto& c = *static_cast<const physics::Character*>(comp.value.Get());
                Put(s.center, space3d::TransformPoint(*w, Vec3{0, c.height * 0.5f, 0}));
                s.type = CollisionType::Capsule;
                s.sizeX = c.radius;
                s.sizeY = std::max(0.f, c.height * 0.5f - c.radius);
                out.push_back(s);
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
