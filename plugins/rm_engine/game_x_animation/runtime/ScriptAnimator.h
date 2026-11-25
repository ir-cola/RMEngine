#pragma once
// ゲーム制作 × アニメーション - スクリプト向けの部分 (sdk/RMAnim.h の rm::Animator): クリップを流す
// (クロスフェード)・一時停止・速さ・時刻・骨の位置と回転。RegisterRuntime がゲーム制作の kScriptApiPoint に出す。

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_animation {

const game_production::ScriptApiPart& ScriptAnimatorPart();

}  // namespace rm::game_x_animation
