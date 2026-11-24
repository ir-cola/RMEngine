#include "rm_engine/weather_x_physics/runtime/Register.h"

#include "core/reflect/Reflect.h"
#include "rm_engine/physics/runtime/Components.h"

#include <string>

namespace rm::weather_x_physics {

void GatherMovers(const game_production::Scene& scene, std::vector<int>& out) {
    const std::string character = reflect::TypeOf<physics::Character>().name, body = reflect::TypeOf<physics::Rigidbody>().name;
    for (const game_production::SceneEntity& e : scene.Entities()) {
        bool moves = false;
        for (const game_production::Component& comp : e.components) {
            if (!comp.Known()) continue;
            if (comp.type == character) moves = true;
            if (comp.type == body && static_cast<const physics::Rigidbody*>(comp.value.Get())->motion != physics::Motion::Static) moves = true;
        }
        if (moves) out.push_back(e.data.id);
    }
}

void RegisterRuntime(RuntimeRegistry& registry) {
    static const weather::WeatherMoversInfo kMovers{&GatherMovers};
    registry.Contribute(weather::kWeatherMoversPoint, kMovers);
}

}  // namespace rm::weather_x_physics

RM_RUNTIME_ENTRY(rm_engine, weather_x_physics, rm::weather_x_physics::RegisterRuntime)
