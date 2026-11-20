#pragma once
// スクリプト API の 3D デバッグ描画の部分 (sdk/RM3D.h: rm::Debug::Line / Box / Sphere / Ray)。
// 形はゲーム制作の DebugDraw (ScriptWorld::debug) で線になる。バッファーが無い - 書き出した
// ゲーム - ときは、どの呼び出しもすぐ返る。RegisterRuntime が kScriptApiPoint に出す。

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_3d {

const game_production::ScriptApiPart& ScriptDebug3DPart();

// 球の 3 つの円 (X、Y、Z のまわり) は、それぞれこの本数の線でできている。
inline constexpr int kDebugSphereSegments = 24;

}  // namespace rm::game_x_3d
