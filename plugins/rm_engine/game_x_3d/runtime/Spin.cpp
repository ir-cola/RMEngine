#include "rm_engine/game_x_3d/runtime/Spin.h"

#include "rm_engine/space3d/runtime/Transform.h"

#include <cmath>

namespace rm::game_x_3d {

namespace {

// Kept between -360 and 360 (the same turn; the numbers in the inspector do not grow without end).
float Wrap(float degrees) { return std::fmod(degrees, 360.f); }

}  // namespace

void RunSpin(game_production::SystemContext& ctx) {
    const std::string transform = reflect::TypeOf<space3d::Transform>().name;
    game_production::ForEachComponent(ctx.scene, reflect::TypeOf<Spin>().name, [&](game_production::SceneEntity& e, game_production::Component& c) {
        game_production::Component* t = e.FindComponent(transform);
        if (!t || !t->Known()) return;
        const auto& spin = *static_cast<const Spin*>(c.value.Get());
        auto& r = static_cast<space3d::Transform*>(t->value.Get())->rotation;
        r.x = Wrap(r.x + spin.degreesPerSecond.x * ctx.dt);
        r.y = Wrap(r.y + spin.degreesPerSecond.y * ctx.dt);
        r.z = Wrap(r.z + spin.degreesPerSecond.z * ctx.dt);
    });
}

}  // namespace rm::game_x_3d
