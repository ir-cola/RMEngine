#pragma once
// コネクター アニメーション × 物理 (docs/ANIMATION.md 7-1。plugins/RuntimePlugins.cpp に並べる。書き出したゲームは
// RM_RUNTIME_ENTRY で呼ぶ): アニメーターのルートモーション (ゲーム制作 × アニメーション の口
// rm_engine.game_x_animation.root_motion) を、同じエンティティの物理の キャラクター の速さにする
// (PhysicsWorld::Move。横だけ: 縦は重力とジャンプ = 物理)。回転はアニメーションが Transform に書く。
//
// 1 フレーム遅れる: スクリプトの Tick → 物理 (Fixed) → アニメーション (Frame) の順なので、フレーム N の速さは
// フレーム N+1 の刻みで使われる。キャラクターの速さは次に Move を呼ぶまで続くので、動かすクリップが止まった
// フレームには、ゲーム制作 × アニメーション が動いた分 0 で 1 度呼ぶ (速さ 0)。

#include "base/Math.h"
#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"

namespace rm::animation_x_physics {

void RegisterRuntime(RuntimeRegistry& registry);

// ルートモーションの行き先 (RootMotionSinkInfo::take): `entity` にキャラクターがあれば、worldMove / dt の横の
// 速さで歩かせて true。無い・dt が 0: false (アニメーションが Transform の位置に足す)。
bool TakeRootMotion(game_production::Scene& scene, int entity, Vec3 worldMove, float dt);

}  // namespace rm::animation_x_physics
