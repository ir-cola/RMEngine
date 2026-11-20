#pragma once
// カメラを順に切り替える: ゲームが動く間、数秒ごとにゲームのカメラ (SystemContext::camera) を
// 次のカメラへ切り替える部品。システムがカメラを切り替えられることを確かめる一番簡単な方法
// (あとでスクリプトが同じ SystemContext::camera を通して自分のやり方でやる)。
// カメラは、Camera 部品を持って動いているエンティティで、階層の順。最後のあとは最初に戻る。
// この部品を持つ最初の 有効 なエンティティが間隔を決める。
// 純粋 - tests/TestSystems.cpp でテストする。

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Systems.h"

namespace rm::game_x_3d {

RM_STRUCT(Label="カメラを順に切り替える")
struct CameraCycle {
    RM_PROPERTY(Edit, Label="間隔 (秒)", Tooltip="Play の間、この秒数ごとにゲームのカメラを次のカメラ (階層の順) に切り替えます", Min=0.1, Max=3600)
    float seconds = 3.f;
};

// システム (Frame): ゲームの時計が間隔の倍数を通ったら、次のカメラへ。
void RunCameraCycle(game_production::SystemContext& ctx);

}  // namespace rm::game_x_3d
