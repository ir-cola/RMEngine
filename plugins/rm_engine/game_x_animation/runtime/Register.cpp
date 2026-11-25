#include "rm_engine/game_x_animation/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_x_3d/runtime/MeshPose.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorPoser.h"

namespace rm::game_x_animation {

void RegisterRuntime(RuntimeRegistry& registry) {
    // アニメーター (同じエンティティのメッシュのモデルを動かす。1 つのエンティティに 1 つ)。
    static const game_production::ComponentInfo kAnimator{.type = &reflect::TypeOf<Animator>, .folder = "3D"};
    registry.Contribute(game_production::kComponentPoint, kAnimator);
    // 骨のあるモデルの姿勢: シーンビュー、ゲームビュー、書き出したゲーム。
    static const game_x_3d::MeshPoseInfo kPose{&CreateAnimatorPoser};
    registry.Contribute(game_x_3d::kMeshPosePoint, kPose);
}

}  // namespace rm::game_x_animation

RM_RUNTIME_ENTRY(rm_engine, game_x_animation, rm::game_x_animation::RegisterRuntime)
