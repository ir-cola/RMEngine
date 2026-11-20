#pragma once
// スクリプト API の 2D デバッグ描画の部分 (sdk/RM2D.h: rm::Debug::Line / Rect / Circle)。図形は
// game production の DebugDraw (ScriptWorld::debug) の線 (z = 0) になる。バッファがなければ
// (書き出したゲーム) どの呼び出しもすぐ戻る。RegisterRuntime が kScriptApiPoint に出す。

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_2d {

const game_production::ScriptApiPart& ScriptDebug2DPart();

// 円はこの本数の線でできている。
inline constexpr int kDebugCircleSegments = 32;

}  // namespace rm::game_x_2d
