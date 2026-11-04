#pragma once
// Editor part of the game production plugin: adds the "ゲーム" software.
// Today it is the empty frame (scene view and game view panels with nothing in them yet);
// ECS, scenes, Play ... come here later (design RME_PLATFORM 4-b).

#include "editor/plugin/Software.h"

namespace rm::game_production {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_production
