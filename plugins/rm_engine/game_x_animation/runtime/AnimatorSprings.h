#pragma once
// ゲーム制作 × アニメーション - 部品「揺れる骨」を、アニメーターの姿勢にかける (docs/ANIMATION.md 7-2-3)。計算は
// 3D アニメーションの Spring.h。ここは鎖を作る: 部品の鎖 (その骨の下全部) と、「モデルの揺れの設定を使う」なら
// モデルの VRM の揺れ (SkinData::springs)。部品の中身かモデルが変わったら作り直す (揺れは始めから)。
// 純粋。tests/TestGameAnimation.cpp でテストする。1 フレームの順: IK の後、FinishPose の前。

#include "rm_engine/animation3d/runtime/Pose.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"

namespace rm::game_x_animation {

// `pose` に揺れる骨をかける (`world`: エンティティのワールド行列)。揺らした: true。
bool ApplyAnimatorSprings(AnimatorRun& run, const SpringBones& springs, const space3d::Mat4& world, float dt, animation3d::Pose& pose);

// スクリプトの ResetSprings (ワープの後): 次のフレームに始め直す。
void ResetSprings(AnimatorRun& run);

}  // namespace rm::game_x_animation
