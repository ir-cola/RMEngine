#pragma once
// コネクター 天候 × 物理 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは
// RM_RUNTIME_ENTRY から呼ぶ): 物理プラグインの コライダー (箱・球・カプセル・メッシュ; トリガー は除く) と
// キャラクター が、シーンの雨と雪が当たるものになる (天候の WeatherBlocks.h)。

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/weather/runtime/WeatherBlocks.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <functional>
#include <vector>

namespace rm::weather_x_physics {

void RegisterRuntime(RuntimeRegistry& registry);

// ワールドの中のシーンのコライダーを形にしたもの (`world`: エンティティの場所。null: 切ってある)。
// カプセルは円柱と、両端の球。
void GatherBlockers(const game_production::Scene& scene, const std::function<const space3d::Mat4*(int entity)>& world,
                    std::vector<weather::WeatherBlocker>& out);

}  // namespace rm::weather_x_physics
