#pragma once
// ゲーム制作 × パーティクル - the shapes particles collide with (the built-in module 当たる: コライダー):
// whoever knows a kind of collider offers a way to turn the scene's into shapes (the connector
// パーティクル × 物理: the physics plugin's コライダー and キャラクター). Gathered once a frame by each picture.
//
//   static const game_x_particles::CollisionShapesInfo kShapes{&Gather};
//   registry.Contribute(game_x_particles::kCollisionShapesPoint, kShapes);

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/particles/runtime/ParticleShader.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <functional>
#include <vector>

namespace rm::game_x_particles {

// RuntimeRegistry point: items are CollisionShapesInfo (static).
inline constexpr char kCollisionShapesPoint[] = "rm_engine.game_x_particles.collision_shapes";

struct CollisionShapesInfo {
    // Adds the scene's shapes to `out`; `world`: where an entity is (null: not shown - switched off).
    void (*gather)(const game_production::Scene& scene, const std::function<const space3d::Mat4*(int entity)>& world,
                   std::vector<particles::CollisionShape>& out) = nullptr;
};

}  // namespace rm::game_x_particles
