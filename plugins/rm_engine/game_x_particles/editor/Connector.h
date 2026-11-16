#pragma once
// The editor part of ゲーム制作 × パーティクル (listed in plugins/BuiltinPlugins.cpp): the パーティクル tab
// (ParticleTab.h) next to the scene and the game.

#include "editor/plugin/Software.h"

namespace rm::game_x_particles {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_particles
