#pragma once
// The connector パーティクル × 物理 (listed in plugins/RuntimePlugins.cpp; the exported game calls it by
// RM_RUNTIME_ENTRY): the physics plugin's コライダー (箱・球・カプセル; not メッシュ, not トリガー) and
// キャラクター become the shapes particles collide with (ゲーム制作 × パーティクル's CollisionShapes.h).

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/particles/runtime/ParticleShader.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <functional>
#include <vector>

namespace rm::particles_x_physics {

void RegisterRuntime(RuntimeRegistry& registry);

// The scene's colliders as shapes in the world (`world`: where an entity is; null: switched off).
void GatherShapes(const game_production::Scene& scene, const std::function<const space3d::Mat4*(int entity)>& world,
                  std::vector<particles::CollisionShape>& out);

}  // namespace rm::particles_x_physics
