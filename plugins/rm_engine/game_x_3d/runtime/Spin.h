#pragma once
// 回り続ける: ゲームが動いている間、エンティティの Transform を回し続ける部品。Play とシステムが
// 動いていることを見るのに一番簡単なもの。1 つのエンティティに複数付けてもよい (足し合わさる:
// Y まわりと X まわりを 1 つずつ付ければ、両方向に回る)。
// 純粋 (I/O も描画もしない)。tests/TestSystems.cpp でテストする。

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Systems.h"

namespace rm::game_x_3d {

RM_STRUCT(Label="回り続ける")
struct Spin {
    RM_PROPERTY(Edit, Label="1 秒に回る角度 (度)", Tooltip="Transform の回転に、1 秒でこれだけ足します (X → Y → Z の順のオイラー角)")
    Vec3 degreesPerSecond{0, 90, 0};
};

// システム (Frame): 回り続ける 1 つごとに、このフレーム分の回転を、エンティティの最初の Transform に足す。
void RunSpin(game_production::SystemContext& ctx);

}  // namespace rm::game_x_3d
