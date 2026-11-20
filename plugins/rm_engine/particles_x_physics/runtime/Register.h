#pragma once
// コネクター パーティクル × 物理 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは
// RM_RUNTIME_ENTRY で呼ぶ): 物理プラグインの コライダー (箱・球・カプセル; メッシュとトリガーは除く) と
// キャラクター を、粒が当たる形にする (ゲーム制作 × パーティクル の CollisionShapes.h)。

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/particles/runtime/ParticleShader.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <functional>
#include <vector>

namespace rm::particles_x_physics {

void RegisterRuntime(RuntimeRegistry& registry);

// シーンのコライダーをワールドの形にしたもの (`world`: エンティティの位置; null: 切ってある)。
void GatherShapes(const game_production::Scene& scene, const std::function<const space3d::Mat4*(int entity)>& world,
                  std::vector<particles::CollisionShape>& out);

}  // namespace rm::particles_x_physics
