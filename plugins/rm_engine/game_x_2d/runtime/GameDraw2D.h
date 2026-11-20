#pragma once
// 書き出したゲームの 2D の描画役 (runtime/Register.cpp が game_production::kGameDrawPoint に出す)。

#include "rm_engine/game_production/runtime/GameDraw.h"

#include <memory>

namespace rm::game_x_2d {

std::unique_ptr<game_production::GameDrawer> CreateGameDraw2D();

}  // namespace rm::game_x_2d
