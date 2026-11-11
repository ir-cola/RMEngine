#pragma once
// The runtime part of ゲーム制作 × 3D: what an exported game needs from it too (listed in
// plugins/RuntimePlugins.cpp; the editor's loader calls it before RegisterEditor).
// Today: the components 回り続ける and カメラを順に切り替える, and their systems.
// ★ The other component types (Transform, メッシュ, カメラ, 光) are still offered by the editor part
// (editor/Connector.cpp: they carry the inspector's labels); they move here when games are exported.

#include "core/RuntimeRegistry.h"

namespace rm::game_x_3d {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_3d
