#pragma once
// 天候 in a 3D picture (ゲーム制作 × 3D's WorldDraw.h: the scene view, the game view, the exported game -
// one drawer each): the 向きの地図 of the colliders the way the rain and the snow fall and the ways the
// rain fell (3D 空間's RenderFrame::mapDirections / mapShapes), 濡れ on every surface
// (RenderFrame::surface), and the drops and flakes themselves (an ExtraDraw).

#include "rm_engine/game_x_3d/runtime/WorldDraw.h"

#include <memory>

namespace rm::weather {

std::unique_ptr<game_x_3d::WorldDrawer> CreateWeatherDraw();

}  // namespace rm::weather
