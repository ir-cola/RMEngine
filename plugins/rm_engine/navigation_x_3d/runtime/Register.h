#pragma once
// ナビ × 3D のランタイム部分: 「メッシュ (3D モデル)」の形を、ナビメッシュを焼く物として差し出す
// (ゲーム制作 × ナビゲーションの口 rm_engine.game_x_navigation.source)。plugins/RuntimePlugins.cpp に並べる。

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_x_navigation/runtime/NavPorts.h"

#include <vector>

namespace rm::navigation_x_3d {

void RegisterRuntime(RuntimeRegistry& registry);

// 口 source の中身 (テスト用に出す)。
void CollectMeshes(const game_x_navigation::NavSourceContext& ctx, std::vector<game_x_navigation::NavShape>& out);

}  // namespace rm::navigation_x_3d
