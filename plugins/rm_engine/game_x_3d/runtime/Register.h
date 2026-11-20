#pragma once
// ゲーム制作 × 3D のランタイム側: 書き出したゲームにも要るもの (plugins/RuntimePlugins.cpp に
// 並ぶ。エディターのローダーが RegisterEditor の前に呼ぶ)。
// 出す部品の型はすべて (Transform, メッシュ, カメラ, 光, 環境光, 回り続ける, カメラを順に切り替える)
// と、システム。エディターでの見せ方 (ローカル / ワールド、階層の絵) はエディター側のもの
// (editor/Connector.cpp: game_production::ComponentLook)。

#include "core/RuntimeRegistry.h"

namespace rm::game_x_3d {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_3d
