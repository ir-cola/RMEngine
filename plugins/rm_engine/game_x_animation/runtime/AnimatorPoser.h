#pragma once
// ゲーム制作 × アニメーション - アニメーターの姿勢を、ゲーム制作 × 3D の口 rm_engine.game_x_3d.mesh_pose
// (MeshPose.h) に答えるもの。描く画面ごとに 1 つ (シーンビュー・カメラのプレビュー・ゲームビュー・書き出した
// ゲーム)。エンティティごとに animation3d::Player を 1 つ持ち、何を見せるかは AnimatorLogic.h の決まり。
//
// 止まっているエディターのプレビューの時計 (EditorPreview) はプログラムに 1 つ: シーンビューの絵
// ("scene") だけが持ち主を選びに合わせて進め、ほかの絵 (カメラのプレビュー) は同じ時刻を見せる。
// シーンビューの上の操作 (editor/Connector.cpp) がそれを止めたり動かしたりする。

#include "rm_engine/game_x_3d/runtime/MeshPose.h"
#include "rm_engine/game_x_animation/runtime/AnimatorLogic.h"

#include <memory>

namespace rm::game_x_animation {

std::unique_ptr<game_x_3d::MeshPoser> CreateAnimatorPoser();

// 止まっているエディターのプレビューの時計 (描画のスレッドだけが触る)。
PreviewClock& EditorPreview();

}  // namespace rm::game_x_animation
