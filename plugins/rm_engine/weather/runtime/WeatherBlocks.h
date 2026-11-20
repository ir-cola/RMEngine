#pragma once
// 天候 - 雨と雪が当たるもの: ある種類のコライダーを知っている側が、シーンのものを形として出す
// (コネクター 天候 × 物理: 物理プラグインの コライダー と キャラクター)。絵ごとにフレームに 1 回集めて、
// 3D 空間に向きの地図の形として渡す (RenderFrame::mapShapes: 描かない)。
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

// RuntimeRegistry の口: 項目は WeatherBlockersInfo (static)。
inline constexpr char kWeatherBlockersPoint[] = "rm_engine.weather.blockers";

// 雨と雪が当たる形。Box / Sphere / Cylinder: `world` が、レンダラーの単純な形 (箱は
// -0.5 .. 0.5、半径 0.5 の球、y が -0.5 から 0.5 の半径 0.5 の円柱) を置き場所へ持っていく。Model:
// エンティティ自身のモデル (そのメッシュ)、`world` はその置き場所。
struct WeatherBlocker {
    enum class Kind : uint8_t { Box, Sphere, Cylinder, Model } kind = Kind::Box;
    space3d::Mat4 world;
    int entity = 0;       // Model: だれの
    bool moving = false;  // 動く (キャラクター、動かない ではない体): 雪に足跡を残す
};

struct WeatherBlockersInfo {
    // シーンの形を `out` に足す。`world`: エンティティの場所 (null: 出ていない - 切ってある)。
    void (*gather)(const game_production::Scene& scene, const std::function<const space3d::Mat4*(int entity)>& world,
                   std::vector<WeatherBlocker>& out) = nullptr;
};

// 絵の形: 出されたものすべてを、レンダラーの単純な形 (`shapeOf`: Renderer::ShapeOf) にするか、
// `modelOf` が言うそのエンティティを描くモデルにする (なければ除く)。`scratch`: フレームをまたいで
// 持つ (確保しない)。
void MakeWeatherBlocks(const game_production::Scene& scene, const RuntimeRegistry& registry,
                       const std::function<const space3d::Mat4*(int entity)>& world,
                       const std::function<const space3d::GpuMesh*(space3d::Shape)>& shapeOf,
                       const std::function<const space3d::GpuMesh*(int entity)>& modelOf, std::vector<space3d::MapShape>& out,
                       std::vector<WeatherBlocker>& scratch);

}  // namespace rm::weather
