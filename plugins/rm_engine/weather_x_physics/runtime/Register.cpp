#include "rm_engine/weather_x_physics/runtime/Register.h"

#include "core/reflect/Reflect.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/physics/runtime/Shapes.h"
#include "rm_engine/space3d/runtime/View.h"

#include <string>

namespace rm::weather_x_physics {

namespace {

using weather::WeatherBlocker;
using space3d::Mat4;

// The unit shape turned by `turn`, `size` big on its own axes, its middle at `center`.
Mat4 Placed(const Mat4& turn, Vec3 size, Vec3 center) {
    Mat4 m = turn;
    const float s[3] = {size.x, size.y, size.z};
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) m.m[c][r] *= s[c];
    }
    m.m[3][0] = center.x;
    m.m[3][1] = center.y;
    m.m[3][2] = center.z;
    return m;
}

Vec3 Plus(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

// A shape (physics' ShapeDims) where its entity is.
void AddShapes(const physics::ShapeDims& d, const physics::Pose& pose, int entity, const Mat4& world, std::vector<WeatherBlocker>& out) {
    const Vec3 center = Plus(pose.position, space3d::TransformDirection(pose.turn, d.offset));
    switch (d.kind) {
        case physics::ShapeDims::Kind::Box:
            out.push_back({WeatherBlocker::Kind::Box, Placed(pose.turn, {d.half.x * 2, d.half.y * 2, d.half.z * 2}, center)});
            break;
        case physics::ShapeDims::Kind::Sphere:
            out.push_back({WeatherBlocker::Kind::Sphere, Placed(pose.turn, {d.radius * 2, d.radius * 2, d.radius * 2}, center)});
            break;
        case physics::ShapeDims::Kind::Capsule: {
            const float r2 = d.radius * 2;
            if (d.halfHeight > 0) out.push_back({WeatherBlocker::Kind::Cylinder, Placed(pose.turn, {r2, d.halfHeight * 2, r2}, center)});
            for (const float end : {d.halfHeight, -d.halfHeight}) {
                const Vec3 at = Plus(center, space3d::TransformDirection(pose.turn, {0, end, 0}));
                out.push_back({WeatherBlocker::Kind::Sphere, Placed(pose.turn, {r2, r2, r2}, at)});
            }
            break;
        }
        case physics::ShapeDims::Kind::Mesh: out.push_back({WeatherBlocker::Kind::Model, world, entity}); break;
    }
}

void Add(const physics::ShapeDims& d, const physics::Pose& pose, int entity, const Mat4& world, bool moving, std::vector<WeatherBlocker>& out) {
    const size_t first = out.size();
    AddShapes(d, pose, entity, world, out);
    for (size_t k = first; k < out.size(); ++k) out[k].moving = moving;
}

}  // namespace

void GatherBlockers(const game_production::Scene& scene, const std::function<const Mat4*(int)>& world, std::vector<WeatherBlocker>& out) {
    const std::string collider = reflect::TypeOf<physics::Collider>().name, character = reflect::TypeOf<physics::Character>().name;
    const std::string body = reflect::TypeOf<physics::Rigidbody>().name;
    for (const game_production::SceneEntity& e : scene.Entities()) {
        const Mat4* w = world ? world(e.data.id) : nullptr;
        if (!w) continue;
        const physics::Pose pose = physics::PoseOf(*w);
        // Does it move (a body that is not 動かない)? Its colliders leave footprints in snow.
        bool moves = false;
        for (const game_production::Component& comp : e.components) {
            if (comp.Known() && comp.type == body) moves = static_cast<const physics::Rigidbody*>(comp.value.Get())->motion != physics::Motion::Static;
        }
        for (const game_production::Component& comp : e.components) {
            if (!comp.Known()) continue;
            if (comp.type == collider) {
                const auto& c = *static_cast<const physics::Collider*>(comp.value.Get());
                if (c.trigger) continue;  // (passed through)
                Add(physics::DimsOf(c, pose.scale), pose, e.data.id, *w, moves, out);
            } else if (comp.type == character) {
                // (the body stands up on the entity's place as the physics has it: not turned, not scaled)
                physics::Pose upright;
                upright.position = pose.position;
                Add(physics::DimsOf(*static_cast<const physics::Character*>(comp.value.Get())), upright, e.data.id, *w, true, out);
            }
        }
    }
}

void RegisterRuntime(RuntimeRegistry& registry) {
    static const weather::WeatherBlockersInfo kBlockers{&GatherBlockers};
    registry.Contribute(weather::kWeatherBlockersPoint, kBlockers);
}

}  // namespace rm::weather_x_physics

RM_RUNTIME_ENTRY(rm_engine, weather_x_physics, rm::weather_x_physics::RegisterRuntime)
