#pragma once
// 回り続ける (2D): ゲームが動く間、エンティティの Transform 2D を回し続ける部品。
// Play が 2D で動くのを見るのに一番簡単なもの。1 つのエンティティに複数付けてもよい (合計される)。
// 純粋 (I/O も描画もしない)。tests/TestSpace2d.cpp でテストする。

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Systems.h"

namespace rm::game_x_2d {

RM_STRUCT(Label="回り続ける (2D)")
struct Spin2D {
    RM_PROPERTY(Edit, Label="1 秒に回る角度 (度)", Tooltip="Transform 2D の回転に、1 秒でこれだけ足します (反時計回りが +)")
    float degreesPerSecond = 90.f;
};

// システム (Frame): すべての 回り続ける (2D) が、このフレーム分の回転をエンティティの最初の Transform 2D に足す。
void RunSpin2D(game_production::SystemContext& ctx);

}  // namespace rm::game_x_2d
