#include "rm_engine/game_x_3d/runtime/WeatherBlocks.h"

#include "rm_engine/space3d/runtime/Primitives.h"

namespace rm::game_x_3d {

void MakeWeatherBlocks(const game_production::Scene& scene, const RuntimeRegistry& registry,
                       const std::function<const space3d::Mat4*(int entity)>& world, const std::function<const space3d::GpuMesh*(space3d::Shape)>& shapeOf,
                       const std::function<const space3d::GpuMesh*(int entity)>& modelOf, std::vector<space3d::WeatherBlock>& out,
                       std::vector<WeatherBlocker>& scratch) {
    out.clear();
    scratch.clear();
    for (const auto& c : registry.Contributions<WeatherBlockersInfo>(kWeatherBlockersPoint)) {
        if (c.item->gather) c.item->gather(scene, world, scratch);
    }
    for (const WeatherBlocker& b : scratch) {
        const space3d::GpuMesh* mesh = nullptr;
        switch (b.kind) {
            case WeatherBlocker::Kind::Box: mesh = shapeOf(space3d::Shape::Box); break;
            case WeatherBlocker::Kind::Sphere: mesh = shapeOf(space3d::Shape::Sphere); break;
            case WeatherBlocker::Kind::Cylinder: mesh = shapeOf(space3d::Shape::Cylinder); break;
            case WeatherBlocker::Kind::Model: mesh = modelOf ? modelOf(b.entity) : nullptr; break;
        }
        if (mesh) out.push_back({mesh, b.world, false});
    }
}

}  // namespace rm::game_x_3d
