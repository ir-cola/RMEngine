#pragma once
// The exported game's drawer for 3D (offered at game_production::kGameDrawPoint by runtime/Register.cpp).

#include "rm_engine/game_production/runtime/GameDraw.h"

#include <memory>

namespace rm::game_x_3d {

std::unique_ptr<game_production::GameDrawer> CreateGameDraw3D();

}  // namespace rm::game_x_3d
