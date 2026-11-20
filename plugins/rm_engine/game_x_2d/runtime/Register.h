#pragma once
// ゲーム制作 × 2D のランタイムの部分: 書き出したゲームにもこれが要る (plugins/RuntimePlugins.cpp に
// 並べる。エディターのローダーが RegisterEditor の前に呼ぶ)。
// 出すのは、すべての部品の種類 (Transform 2D、スプライト、スプライトのアニメーション、カメラ (2D)、
// 回り続ける (2D)) とシステム。エディターでの見せ方 (ローカル / ワールド、階層の絵) は
// エディター側の部分 (editor/Connector.cpp: game_production::ComponentLook)。

#include "core/RuntimeRegistry.h"

namespace rm::game_x_2d {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_2d
