#pragma once
// The runtime part of ゲーム制作 × 2D: what an exported game needs from it too (listed in
// plugins/RuntimePlugins.cpp; the editor's loader calls it before RegisterEditor).
// Every component type it offers (Transform 2D, スプライト, スプライトのアニメーション, カメラ (2D),
// 回り続ける (2D)) and the systems. How the editor shows them (ローカル / ワールド, the 階層's pictures)
// is the editor part's (editor/Connector.cpp: game_production::ComponentLook).

#include "core/RuntimeRegistry.h"

namespace rm::game_x_2d {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_2d
