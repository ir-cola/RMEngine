#pragma once
// The runtime part of ゲーム制作 × 3D: what an exported game needs from it too (listed in
// plugins/RuntimePlugins.cpp; the editor's loader calls it before RegisterEditor).
// Every component type it offers (Transform, メッシュ, カメラ, 光, 環境光, 回り続ける, カメラを順に切り替える)
// and the systems. How the editor shows them (ローカル / ワールド, the 階層's pictures) is the editor
// part's (editor/Connector.cpp: game_production::ComponentLook).

#include "core/RuntimeRegistry.h"

namespace rm::game_x_3d {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_3d
