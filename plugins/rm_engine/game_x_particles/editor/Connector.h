#pragma once
// ゲーム制作 × パーティクル のエディターの部分 (plugins/BuiltinPlugins.cpp に並べる): シーンと
// ゲームの隣の パーティクル タブ (ParticleTab.h)。

#include "editor/plugin/Software.h"

namespace rm::game_x_particles {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_particles
