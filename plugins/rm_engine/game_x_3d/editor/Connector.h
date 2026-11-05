#pragma once
// The connector "ゲーム制作 × 3D" (design RME_PLATFORM 4-f, plan C): the parts of game production
// that need to know 3D space. Game production itself never includes 3D 空間; this plugin requires
// both and joins them. It is installed automatically once both are (never chosen by hand), and
// game production is enabled only while a connector like this one is.
//
// Today it offers 3D 空間's Transform as a component that every new entity gets. The scene view,
// the game camera and debug drawing in 3D come here later (the four parts in the table of 4-f).

#include "editor/plugin/Software.h"

namespace rm::game_x_3d {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_3d
