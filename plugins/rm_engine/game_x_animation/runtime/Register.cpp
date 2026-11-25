#include "rm_engine/game_x_animation/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/MeshPose.h"
#include "rm_engine/game_x_animation/runtime/AnimationSystem.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorPoser.h"
#include "rm_engine/game_x_animation/runtime/ScriptAnimator.h"

namespace rm::game_x_animation {

void RegisterRuntime(RuntimeRegistry& registry) {
    // アニメーター (同じエンティティのメッシュのモデルを動かす。1 つのエンティティに 1 つ) と、骨に付ける。
    static const game_production::ComponentInfo kAnimator{.type = &reflect::TypeOf<Animator>, .folder = "3D"};
    registry.Contribute(game_production::kComponentPoint, kAnimator);
    static const game_production::ComponentInfo kAttach{.type = &reflect::TypeOf<BoneAttach>, .folder = "3D"};
    registry.Contribute(game_production::kComponentPoint, kAttach);
    // ゲームが動いている間: 進める (クリップのイベントも) → 骨に付ける (AnimationSystem.h)。
    static const game_production::SystemInfo kAnimation{"アニメーション", game_production::SystemPhase::Frame, 100, &RunAnimation};
    registry.Contribute(game_production::kSystemPoint, kAnimation);
    static const game_production::SystemInfo kBoneAttach{"骨に付ける", game_production::SystemPhase::Frame, 110, &RunBoneAttach};
    registry.Contribute(game_production::kSystemPoint, kBoneAttach);
    // 骨のあるモデルの姿勢: シーンビュー、ゲームビュー、書き出したゲーム。
    static const game_x_3d::MeshPoseInfo kPose{&CreateAnimatorPoser};
    registry.Contribute(game_x_3d::kMeshPosePoint, kPose);
    // スクリプト: クリップを流す / 骨の位置 (sdk/RMAnim.h)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptAnimatorPart());
}

}  // namespace rm::game_x_animation

RM_RUNTIME_ENTRY(rm_engine, game_x_animation, rm::game_x_animation::RegisterRuntime)
