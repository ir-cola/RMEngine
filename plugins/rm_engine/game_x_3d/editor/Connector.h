#pragma once
// コネクター「ゲーム制作 × 3D」 (設計 RME_PLATFORM 4-f、案 C): ゲーム制作のうち 3D の空間を
// 知る必要がある部分。ゲーム制作そのものは 3D 空間を含まない。このプラグインは両方を requires
// して結びつける。両方が入ると自動で入り (手では選ばない)、ゲーム制作はこのようなコネクターが
// ある間だけ有効になる。
//
// 3D 空間の Transform を、新しいエンティティすべてが持つ部品として出す。3D のシーンビュー
// (描くのは 3D 空間) も出す。3D のゲームカメラとデバッグ描画は、あとでここに入る (4-f の表)。

#include "editor/plugin/Software.h"

namespace rm::game_x_3d {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_3d
