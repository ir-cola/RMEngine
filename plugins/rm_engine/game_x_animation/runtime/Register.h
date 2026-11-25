#pragma once
// ゲーム制作 × アニメーション のランタイム部分 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは
// RM_RUNTIME_ENTRY で呼ぶ): 部品 アニメーター (Animator.h) と、その姿勢をゲーム制作 × 3D の絵に渡すもの
// (AnimatorPoser.h: 口 rm_engine.game_x_3d.mesh_pose - エディターのビューもゲームも同じ)。

#include "core/RuntimeRegistry.h"

namespace rm::game_x_animation {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_animation
