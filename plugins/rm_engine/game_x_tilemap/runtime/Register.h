#pragma once
// ゲーム制作 × タイルマップ のランタイム部分 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは
// RM_RUNTIME_ENTRY で呼ぶ): 部品「タイルのグリッド」「タイルの層」(TileLayer.h) と、層を 2D の絵に描く
// 処理 (TileDraw.h: ゲーム制作 × 2D の口 world_draw - エディターのビューもゲームも同じ)。

#include "core/RuntimeRegistry.h"

namespace rm::game_x_tilemap {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_tilemap
