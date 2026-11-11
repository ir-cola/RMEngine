#pragma once
// カメラを順に切り替える: a component that switches the game's camera (SystemContext::camera) to the
// next camera every few seconds while the game plays - the simplest thing to see that a system can
// switch cameras (later, scripts will do it their own way through the same SystemContext::camera).
// The cameras are the entities with a Camera component that run, in the 階層's order; after the last
// comes the first. The first 有効 entity with this component decides the interval.
// Pure - tested in tests/TestSystems.cpp.

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Systems.h"

namespace rm::game_x_3d {

RM_STRUCT(Label="カメラを順に切り替える")
struct CameraCycle {
    RM_PROPERTY(Edit, Label="間隔 (秒)", Tooltip="Play の間、この秒数ごとにゲームのカメラを次のカメラ (階層の順) に切り替えます", Min=0.1, Max=3600)
    float seconds = 3.f;
};

// The system (Frame): when the game's clock passes a multiple of the interval, the next camera.
void RunCameraCycle(game_production::SystemContext& ctx);

}  // namespace rm::game_x_3d
