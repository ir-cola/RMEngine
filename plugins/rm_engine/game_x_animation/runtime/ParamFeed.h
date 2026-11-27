#pragma once
// ゲーム制作 × アニメーション - パラメーターの送り手の口 (docs/ANIMATION.md 7-6)。ゲーム制作 × アニメーション は物理もナビも
// 知らない。つなぎが「毎フレーム読める値」(ナビの速さ・物理のキャラクターの速さ ...) を差し出し、部品「アニメーター」の
// 「パラメーターの送り手」が、どの値をどのパラメーターに入れるかを決める。システム「アニメーション」が、ルールを調べる前に
// 毎フレーム読んで入れる (SetRuleParam)。送り手が無い (そのプラグインが無い) 行は何もしない (欄はそのまま残す)。
//
//   // つなぎ ナビ × アニメーション (navigation_x_animation) の runtime/Register.cpp で:
//   static const game_x_animation::ParamFeedInfo kSpeed{"rm_engine.navigation_x_animation.speed", "ナビ: 速さ (m/s)", &AgentSpeed};
//   registry.Contribute(game_x_animation::kParamFeedPoint, kSpeed);

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <string>

namespace rm::game_x_animation {

// RuntimeRegistry の口: 項目は ParamFeedInfo (static)。
inline constexpr char kParamFeedPoint[] = "rm_engine.game_x_animation.param_feed";

struct ParamFeedInfo {
    const char* id = "";     // 保存する名前 ("rm_engine.navigation_x_animation.speed" など。変えない)
    const char* label = "";  // 画面の名前 (UTF-8・日本語。エディターが訳す)
    float (*read)(game_production::Scene& scene, int entity) = nullptr;
};

RM_STRUCT(Label="パラメーターの送り手")
struct ParamFeed {
    RM_PROPERTY(Edit, Label="パラメーター", Tooltip="状態とルールのパラメーターの名前 (speed など)") std::string param;
    RM_PROPERTY(Edit, Label="入れるもの", Tooltip="毎フレームこのパラメーターに入れる値 (ナビの速さ・向きの差 など。入っているプラグインが出すもの)")
    std::string source;
};

}  // namespace rm::game_x_animation
