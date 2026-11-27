#pragma once
// ナビ × 3D - シーンビュー (WorldDrawFrame::picture == "scene") に、焼いたナビメッシュの面を塗って見せる描き手。
// 部品「ナビメッシュ」の焼いた物 (.rmnav) を読み、多角形をエリアの種類の色で薄く塗る (深度テストあり・書かない)。
// 塗るかどうかは game_x_navigation::FillInSceneView() (「ナビ」タブの「シーンビューで面を塗る」)。
// ゲームビューと書き出したゲームには描かない。

#include "rm_engine/game_x_3d/runtime/WorldDraw.h"

#include <memory>

namespace rm::navigation_x_3d {

std::unique_ptr<game_x_3d::WorldDrawer> CreateNavFill();

}  // namespace rm::navigation_x_3d
