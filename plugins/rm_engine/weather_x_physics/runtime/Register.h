#pragma once
// The connector 天候 × 物理 (listed in plugins/RuntimePlugins.cpp; the exported game calls it by
// RM_RUNTIME_ENTRY): the physics plugin's コライダー (箱・球・カプセル・メッシュ; not トリガー) and
// キャラクター become what the scene's rain and snow hit (天候's WeatherBlocks.h).

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/weather/runtime/WeatherBlocks.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <functional>
#include <vector>

namespace rm::weather_x_physics {

void RegisterRuntime(RuntimeRegistry& registry);

// The scene's colliders as shapes in the world (`world`: where an entity is; null: switched off). A
// capsule is a cylinder and a ball at each end.
void GatherBlockers(const game_production::Scene& scene, const std::function<const space3d::Mat4*(int entity)>& world,
                    std::vector<weather::WeatherBlocker>& out);

}  // namespace rm::weather_x_physics
