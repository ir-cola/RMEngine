#include "rm_engine/weather/runtime/WeatherBlocks.h"

namespace rm::weather {

void MakeWeatherBlocks(const game_production::Scene& scene, const RuntimeRegistry& registry,
                       const std::function<const space3d::Mat4*(int entity)>& world,
                       const std::function<const space3d::GpuMesh*(space3d::Shape)>& shapeOf,
                       const std::function<const space3d::GpuMesh*(int entity)>& modelOf, std::vector<space3d::MapShape>& out,
                       std::vector<WeatherBlocker>& scratch) {
    out.clear();
    scratch.clear();
    for (const auto& c : registry.Contributions<WeatherBlockersInfo>(kWeatherBlockersPoint)) {
        if (c.item->gather) c.item->gather(scene, world, scratch);
    }
    for (const WeatherBlocker& b : scratch) {
        const space3d::GpuMesh* mesh = nullptr;
        switch (b.kind) {
            case WeatherBlocker::Kind::Box: mesh = shapeOf ? shapeOf(space3d::Shape::Box) : nullptr; break;
            case WeatherBlocker::Kind::Sphere: mesh = shapeOf ? shapeOf(space3d::Shape::Sphere) : nullptr; break;
            case WeatherBlocker::Kind::Cylinder: mesh = shapeOf ? shapeOf(space3d::Shape::Cylinder) : nullptr; break;
            case WeatherBlocker::Kind::Model: mesh = modelOf ? modelOf(b.entity) : nullptr; break;
        }
        if (mesh) out.push_back({mesh, b.world, b.moving});
    }
}

}  // namespace rm::weather
