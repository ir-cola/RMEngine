#pragma once
// コネクター「ゲーム制作 × 2D」(設計 RME_PLATFORM 4-f、案 C): ゲーム制作のうち 2D の空間を知る必要が
// ある部分。ゲーム制作自身は 2D 空間 を決して取り込まない。このプラグインが両方を requires して
// つなぐ。両方が入ると自動で入る (手では選ばない)。
//
// 2D 空間の Transform 2D を、2D のゲームの新しいエンティティが持つ部品として、スプライト (絵)、
// 回り続ける (2D) とそのシステム、そして 2D シーンビュー (2D 空間が描く) を提供する。

#include "editor/plugin/Software.h"

namespace rm::game_x_2d {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_2d
