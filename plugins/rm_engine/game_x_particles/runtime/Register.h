#pragma once
// ゲーム制作 × パーティクル のランタイム部分 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは
// RM_RUNTIME_ENTRY で呼ぶ): 部品 パーティクル (ParticleSystem.h) と、それを 3D の絵に描く処理
// (ParticleDraw.h: ゲーム制作 × 3D の WorldDrawer - エディターのビューもゲームも同じ)。

#include "core/RuntimeRegistry.h"

namespace rm::game_x_particles {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_particles
