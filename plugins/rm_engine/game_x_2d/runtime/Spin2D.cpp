#include "rm_engine/game_x_2d/runtime/Spin2D.h"

#include "rm_engine/space2d/runtime/Transform2D.h"

#include <cmath>

namespace rm::game_x_2d {

void RunSpin2D(game_production::SystemContext& ctx) {
    const std::string transform = reflect::TypeOf<space2d::Transform2D>().name;
    game_production::ForEachComponent(ctx.scene, reflect::TypeOf<Spin2D>().name, [&](game_production::SceneEntity& e, game_production::Component& c) {
        game_production::Component* t = e.FindComponent(transform);
        if (!t || !t->Known()) return;
        const auto& spin = *static_cast<const Spin2D*>(c.value.Get());
        float& r = static_cast<space2d::Transform2D*>(t->value.Get())->rotation;
        r = std::fmod(r + spin.degreesPerSecond * ctx.dt, 360.f);  // kept between -360 and 360
    });
}

}  // namespace rm::game_x_2d
