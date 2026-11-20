#pragma once
// ゲーム制作 × パーティクル - パーティクルがぶつかる形 (組み込みモジュール 当たる: コライダー):
// コライダーの種類を知っている側が、シーンのものを形にする方法を出す (コネクター
// パーティクル × 物理: 物理プラグインの コライダー と キャラクター)。各絵が 1 フレームに 1 回集める。
//
//   static const game_x_particles::CollisionShapesInfo kShapes{&Gather};
//   registry.Contribute(game_x_particles::kCollisionShapesPoint, kShapes);

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/particles/runtime/ParticleShader.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <functional>
#include <vector>

namespace rm::game_x_particles {

// RuntimeRegistry の口: 項目は CollisionShapesInfo (static)。
inline constexpr char kCollisionShapesPoint[] = "rm_engine.game_x_particles.collision_shapes";

struct CollisionShapesInfo {
    // シーンの形を `out` に足す。`world`: エンティティがどこにあるか (null: 出ていない - オフ)。
    void (*gather)(const game_production::Scene& scene, const std::function<const space3d::Mat4*(int entity)>& world,
                   std::vector<particles::CollisionShape>& out) = nullptr;
};

}  // namespace rm::game_x_particles
