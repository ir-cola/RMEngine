#pragma once
// ゲーム制作 × アニメーション - 部品 アニメーター (docs/ANIMATION.md 段階 1): 同じエンティティの
// 「メッシュ (3D モデル)」の骨のあるモデルを、そのモデルのクリップで動かす。クリップは名前で指す
// (モデルを読み直しても、同じ名前のクリップを流す)。
//
//   Play・書き出したゲーム          始めから流す がついたものすべて (外したものは最初のコマで止まる)
//   止まっているエディター          選んだもの: シーンビューで流れる (再生 / 一時停止・時間のつまみ)
//                                    選んでいないもの: クリップの最初のコマ (クリップが無ければバインドの形)
//
// 姿勢はゲーム制作 × 3D の口 rm_engine.game_x_3d.mesh_pose に答えて渡す (AnimatorPoser.h)。

#include "core/reflect/Reflect.h"

#include <string>

namespace rm::game_x_animation {

RM_STRUCT(Label="アニメーター")
struct Animator {
    RM_PROPERTY(Edit, Label="クリップ", Tooltip="流すクリップ (同じエンティティの「メッシュ (3D モデル)」のモデルが持つもの)。空なら、モデルの最初のクリップ")
    std::string clip;
    RM_PROPERTY(Edit, Label="ループ", Tooltip="外すと、最後まで流して止まります") bool loop = true;
    RM_PROPERTY(Edit, Label="速さ", Tooltip="再生の速さの倍率 (2 で倍の速さ。負の数で逆に流れます)", Min=-10, Max=10, Step=0.05) float speed = 1;
    RM_PROPERTY(Edit, Label="始めから流す", Tooltip="外すと、ゲームでは最初のコマで止まったままです (止まっているシーンビューでは、選ぶと流れます)")
    bool playOnStart = true;
};

}  // namespace rm::game_x_animation
