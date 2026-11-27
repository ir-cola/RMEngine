#pragma once
// タイルマップ × 物理 (つなぎ) のランタイム部分 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは
// RM_RUNTIME_ENTRY で呼ぶ): 「当たりを作る」の付いた有効なタイルの層の区画の当たり (TileColliders) を、物理の
// 口 rm_engine.physics.static_shapes に渡す。区画 1 つ = 動かない物体 1 つ。当たった相手は層のエンティティ。

#include "core/RuntimeRegistry.h"

namespace rm::tilemap_x_physics {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::tilemap_x_physics
