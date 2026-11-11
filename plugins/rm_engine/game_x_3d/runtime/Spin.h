#pragma once
// 回り続ける: a component that keeps turning its entity's Transform while the game plays - the
// simplest thing to see that Play and the systems work. An entity may have several (they add up:
// one about Y and one about X turns it both ways).
// Pure - tested in tests/TestSystems.cpp.

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Systems.h"

namespace rm::game_x_3d {

RM_STRUCT(Label="回り続ける")
struct Spin {
    RM_PROPERTY(Edit, Label="1 秒に回る角度 (度)", Tooltip="Transform の回転に、1 秒でこれだけ足します (X → Y → Z の順のオイラー角)")
    Vec3 degreesPerSecond{0, 90, 0};
};

// The system (Frame): every 回り続ける adds its turn for this frame to the entity's first Transform.
void RunSpin(game_production::SystemContext& ctx);

}  // namespace rm::game_x_3d
