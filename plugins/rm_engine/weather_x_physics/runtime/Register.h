#pragma once
// コネクター 天候 × 物理 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは
// RM_RUNTIME_ENTRY から呼ぶ): どのエンティティが動くか (動かない ではない 剛体、キャラクター) を天候に
// 言う - そのメッシュは雪に足跡を残す (天候の WeatherBlocks.h)。雨と雪が当たるのは見えているメッシュで、
// コライダーではない (2026-11-25 から)。

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/weather/runtime/WeatherBlocks.h"

#include <vector>

namespace rm::weather_x_physics {

void RegisterRuntime(RuntimeRegistry& registry);

// シーンの中の動くエンティティの id (`out` に足す)。
void GatherMovers(const game_production::Scene& scene, std::vector<int>& out);

}  // namespace rm::weather_x_physics
