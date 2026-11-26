#pragma once
// ゲーム制作 × アニメーション - ルートモーションの行き先の口 (docs/ANIMATION.md 7-1・7-6)。
// ゲーム制作 × アニメーション は物理もナビも知らない。システム「アニメーション」(order 100) は、アニメーターの
// ルートモーションが「動かす」で根が動いたフレームに、寄せられた行き先 (RootMotionSinkInfo) に順に尋ね、
// どれかが受け取ったら (true) **位置を書かない**。誰も受け取らなければ Transform の位置に足す。
// 回転はいつもアニメーションが Transform に書く (物理のキャラクターは回転を持たない)。
//
//   // つなぎ アニメーション × 物理 (animation_x_physics) の runtime/Register.cpp で:
//   static const game_x_animation::RootMotionSinkInfo kSink{&TakeRootMotion};
//   registry.Contribute(game_x_animation::kRootMotionPoint, kSink);
//
// 動かすクリップが止まったフレーム (前のフレームは動かしていて、今は動かすクリップに重みが無い) に、1 度だけ
// 動いた分 0 で尋ねる (キャラクターが滑り続けないように: 速さ 0)。
//
// ナビゲーション (docs/NAVIGATION.md の口 rm_engine.game_x_navigation.mover。ナビ × アニメーション
// navigation_x_animation が作る) との決まり: 「動く者を動かす人は 1 人だけ」。ルートモーションで歩かせるとき
// (ANIMATION.md 7-6 の案 B)、ナビの動かし方は DrivenByRootMotion が true の物を自分では動かさず、欲しい速さを
// アニメーターのパラメーターに入れるだけにする (動かすのはこのシステムと、上の行き先)。ナビのシステムは
// order 100 より前に置く。

#include "core/RuntimeRegistry.h"
#include "base/Math.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_animation {

// RuntimeRegistry の口: 項目は RootMotionSinkInfo (static)。
inline constexpr char kRootMotionPoint[] = "rm_engine.game_x_animation.root_motion";

struct RootMotionSinkInfo {
    // エンティティ `entity` を、このフレーム `worldMove` (ワールド。dt 秒で) だけ動かしたい。受け取ったら true
    // (アニメーションは位置を書かない)。動かすクリップが止まったフレームには worldMove = 0 で 1 度だけ呼ぶ。
    bool (*take)(game_production::Scene& scene, int entity, Vec3 worldMove, float dt) = nullptr;
};

// このエンティティはルートモーションで動く (アニメーターがあり、ルートモーションが「動かす」で、今流している
// クリップのどれかが「使う」)。ナビの動かし方が使う (上の決まり)。
bool DrivenByRootMotion(const game_production::Scene& scene, int entity);

// ルートモーションを足した後の Transform (純粋。tests/TestGameAnimation.cpp。AttachedTransform と同じ形):
// ワールドで `worldMove` だけ動かし (writePosition が false なら位置はそのまま)、自分の Y のまわりに
// `turnRadians` 回す。`parentWorld` はそのエンティティの親のワールド行列。大きさはそのまま。
space3d::Transform RootMotionTransform(const space3d::Transform& current, const space3d::Mat4& parentWorld, Vec3 worldMove, float turnRadians,
                                       bool writePosition = true);

}  // namespace rm::game_x_animation
