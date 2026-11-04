#pragma once
// Editor part of the game production plugin: adds the "ゲーム" software.
// Today: the game settings, and one scene whose entities (name, switch, parent) are listed in the
// core 階層 and edited in the core インスペクター. The scene view, components (ECS), Play ...
// come here later (design RME_PLATFORM 4-b).

#include "editor/plugin/Software.h"

namespace rm::game_production {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_production
