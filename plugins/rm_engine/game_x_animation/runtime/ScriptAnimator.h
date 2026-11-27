#pragma once
// ゲーム制作 × アニメーション - スクリプト向けの部分 (sdk/RMAnim.h の rm::Animator): クリップを流す
// (クロスフェード)・一時停止・速さ・時刻・骨の位置と回転。RegisterRuntime がゲーム制作の kScriptApiPoint に出す。

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_animation {

const game_production::ScriptApiPart& ScriptAnimatorPart();
// 状態とルール (rm::Animator::SetFloat / SetTrigger / State / Play)。
const game_production::ScriptApiPart& ScriptAnimatorRulesPart();
// ルートモーションとレイヤーの重み (rm::Animator::SetRootMotion / RootDelta / RootTurn / SetLayerWeight)。
const game_production::ScriptApiPart& ScriptAnimatorRootPart();
// IK (rm::Animator::SetIkTarget / SetIkWeight / LookAt)。
const game_production::ScriptApiPart& ScriptAnimatorIkPart();
// ブレンドシェイプ (rm::Animator::SetBlendShape / ClearBlendShape / BlendShape)。
const game_production::ScriptApiPart& ScriptAnimatorMorphPart();

}  // namespace rm::game_x_animation
