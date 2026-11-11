#pragma once
// 回り続ける (2D): a component that keeps turning its entity's Transform 2D while the game plays -
// the simplest thing to see that Play works in 2D. An entity may have several (they add up).
// Pure - tested in tests/TestSpace2d.cpp.

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Systems.h"

namespace rm::game_x_2d {

RM_STRUCT(Label="回り続ける (2D)")
struct Spin2D {
    RM_PROPERTY(Edit, Label="1 秒に回る角度 (度)", Tooltip="Transform 2D の回転に、1 秒でこれだけ足します (反時計回りが +)")
    float degreesPerSecond = 90.f;
};

// The system (Frame): every 回り続ける (2D) adds its turn for this frame to the entity's first Transform 2D.
void RunSpin2D(game_production::SystemContext& ctx);

}  // namespace rm::game_x_2d
