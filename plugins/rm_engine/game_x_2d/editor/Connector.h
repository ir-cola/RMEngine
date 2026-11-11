#pragma once
// The connector "ゲーム制作 × 2D" (design RME_PLATFORM 4-f, plan C): the parts of game production
// that need to know 2D space. Game production itself never includes 2D 空間; this plugin requires
// both and joins them. It is installed automatically once both are (never chosen by hand).
//
// It offers 2D 空間's Transform 2D as a component that new entities of a 2D game get, the sprite (a
// picture), 回り続ける (2D) and its system, and the 2D scene view (drawn by 2D 空間).

#include "editor/plugin/Software.h"

namespace rm::game_x_2d {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_2d
