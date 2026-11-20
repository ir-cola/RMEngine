#pragma once
// 書き出したゲームの 3D の描き手 (runtime/Register.cpp が game_production::kGameDrawPoint に出す)。

#include "rm_engine/game_production/runtime/GameDraw.h"

#include <memory>

namespace rm::game_x_3d {

std::unique_ptr<game_production::GameDrawer> CreateGameDraw3D();

}  // namespace rm::game_x_3d
