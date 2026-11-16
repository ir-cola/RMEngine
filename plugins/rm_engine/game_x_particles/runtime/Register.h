#pragma once
// The runtime part of ゲーム制作 × パーティクル (listed in plugins/RuntimePlugins.cpp; the exported game
// calls it by RM_RUNTIME_ENTRY): the component パーティクル (ParticleSystem.h) and its drawing into the
// 3D pictures (ParticleDraw.h: ゲーム制作 × 3D's WorldDrawer - the editor's views and the game alike).

#include "core/RuntimeRegistry.h"

namespace rm::game_x_particles {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_particles
