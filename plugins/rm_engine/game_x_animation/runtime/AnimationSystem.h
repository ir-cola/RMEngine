#pragma once
// ゲーム制作 × アニメーション - ゲームが動いている間のシステム (docs/ANIMATION.md 3-10)。どちらも Frame で、
// スクリプトの Tick (パラメーターを入れる・PlayClip) の後:
//
//   アニメーション  (order 100)  アニメーターごとに、モデルの骨とクリップを読み (1 度)、時刻を進め、通った
//                                クリップのイベントをそのエンティティのスクリプトの OnEvent へ送り、骨の行列を作る
//   骨に付ける      (order 110)  骨に付ける を持つエンティティを、祖先のアニメーターの骨に合わせる
//
// 描く画面 (AnimatorPoser) は、アニメーションが作った骨の行列を使うだけ (姿勢を 2 回計算しない)。

#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_animation {

void RunAnimation(game_production::SystemContext& ctx);
void RunBoneAttach(game_production::SystemContext& ctx);

// 骨に付けたものの Transform (純粋。tests/TestGameAnimation.cpp): 骨のワールド行列 `boneWorld` に、ずれ
// (位置・回転) を掛けた所へ置く。`parentWorld` はそのエンティティの親のワールド行列。大きさはそのまま、
// followRotation が外れていれば回転もそのまま (位置だけ)。
space3d::Transform AttachedTransform(const space3d::Transform& current, const space3d::Mat4& parentWorld, const space3d::Mat4& boneWorld,
                                     const BoneAttach& attach);

}  // namespace rm::game_x_animation
