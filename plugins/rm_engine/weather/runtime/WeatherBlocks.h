#pragma once
// 天候 - what rain and snow hit: whoever knows a kind of collider offers the scene's as shapes (the
// connector 天候 × 物理: the physics plugin's コライダー and キャラクター). Gathered once a frame by each
// picture and handed to 3D 空間 as the 向きの地図's shapes (RenderFrame::mapShapes: never drawn).
//
//   static const weather::WeatherBlockersInfo kBlockers{&Gather};
//   registry.Contribute(weather::kWeatherBlockersPoint, kBlockers);

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/space3d/runtime/Primitives.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace rm::weather {

// RuntimeRegistry point: items are WeatherBlockersInfo (static).
inline constexpr char kWeatherBlockersPoint[] = "rm_engine.weather.blockers";

// A shape rain and snow hit. Box / Sphere / Cylinder: `world` takes the renderer's simple shape (a box
// -0.5 .. 0.5, a ball of radius 0.5, a cylinder of radius 0.5 from y -0.5 to 0.5) where it is. Model:
// the entity's own model (its メッシュ), `world` its place.
struct WeatherBlocker {
    enum class Kind : uint8_t { Box, Sphere, Cylinder, Model } kind = Kind::Box;
    space3d::Mat4 world;
    int entity = 0;       // Model: whose
    bool moving = false;  // it moves (a character, a body that is not 動かない): it leaves footprints in snow
};

struct WeatherBlockersInfo {
    // Adds the scene's shapes to `out`; `world`: where an entity is (null: not shown - switched off).
    void (*gather)(const game_production::Scene& scene, const std::function<const space3d::Mat4*(int entity)>& world,
                   std::vector<WeatherBlocker>& out) = nullptr;
};

// The picture's shapes: every offered one as the renderer's simple shape (`shapeOf`: Renderer::ShapeOf)
// or as the model `modelOf` says the entity is drawn with (none: left out). `scratch`: kept between
// frames (no allocation).
void MakeWeatherBlocks(const game_production::Scene& scene, const RuntimeRegistry& registry,
                       const std::function<const space3d::Mat4*(int entity)>& world,
                       const std::function<const space3d::GpuMesh*(space3d::Shape)>& shapeOf,
                       const std::function<const space3d::GpuMesh*(int entity)>& modelOf, std::vector<space3d::MapShape>& out,
                       std::vector<WeatherBlocker>& scratch);

}  // namespace rm::weather
