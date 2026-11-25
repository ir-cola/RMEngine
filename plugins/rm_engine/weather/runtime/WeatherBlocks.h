#pragma once
// 天候 - 雨と雪が当たるもの: 画面に見えているメッシュ (単純な形もモデルも。描き手がエンティティごとに
// 言う WorldDrawFrame::model)。絵ごとにフレームに 1 回集めて、3D 空間に向きの地図の形として渡す
// (RenderFrame::mapShapes: 描かない)。2026-11-25 までは物理のコライダーだった: それだと、見えない所
// (コライダーだけがある透明な部分) にも雨が当たり雪が積もった。
//
// ほかのプラグイン (コネクター 天候 × 物理) は、どのエンティティが動くかを言う: そのメッシュと、
// その子のメッシュは雪に足跡を残す (向きの地図の、動くものだけを下から見る地図に入る)。
//
//   static const weather::WeatherMoversInfo kMovers{&Gather};
//   registry.Contribute(weather::kWeatherMoversPoint, kMovers);

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <functional>
#include <span>
#include <vector>

namespace rm::weather {

// RuntimeRegistry の口: 項目は WeatherMoversInfo (static)。
inline constexpr char kWeatherMoversPoint[] = "rm_engine.weather.movers";

struct WeatherMoversInfo {
    // シーンの中の、動くエンティティ (動かない ではない体、キャラクター) の id を `out` に足す。
    void (*gather)(const game_production::Scene& scene, std::vector<int>& out) = nullptr;
};

// 絵の形: 見えているエンティティ (`world` と `modelOf` の両方がある) それぞれのメッシュを、その
// 置き場所に。`movers` に入っているエンティティか、その祖先が入っているものは動く。純粋 (テストする)。
void WeatherBlocksOf(const game_production::Scene& scene, std::span<const int> movers,
                     const std::function<const space3d::Mat4*(int entity)>& world,
                     const std::function<const space3d::GpuMesh*(int entity)>& modelOf, std::vector<space3d::MapShape>& out);

// 上と同じ。動くものはプラグインに尋ねる。`scratch`: フレームをまたいで持つ (確保しない)。
void MakeWeatherBlocks(const game_production::Scene& scene, const RuntimeRegistry& registry,
                       const std::function<const space3d::Mat4*(int entity)>& world,
                       const std::function<const space3d::GpuMesh*(int entity)>& modelOf, std::vector<space3d::MapShape>& out,
                       std::vector<int>& scratch);

}  // namespace rm::weather
