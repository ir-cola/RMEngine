#pragma once
// 3D の絵の中の 天候 (ゲーム制作 × 3D の WorldDraw.h: シーンビュー、ゲームビュー、書き出した
// ゲーム - どれも描き手は 1 つ): 雨と雪が降る向きと、雨が降った向きでの コライダー の向きの地図
// (3D 空間の RenderFrame::mapDirections / mapShapes)、すべての面の 濡れ (RenderFrame::surface)、
// そして粒とひらひら自身 (ExtraDraw)。

#include "rm_engine/game_x_3d/runtime/WorldDraw.h"

#include <memory>

namespace rm::weather {

std::unique_ptr<game_x_3d::WorldDrawer> CreateWeatherDraw();

}  // namespace rm::weather
