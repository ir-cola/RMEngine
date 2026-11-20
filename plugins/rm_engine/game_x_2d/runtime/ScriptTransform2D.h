#pragma once
// スクリプト API の 2D の部分 (sdk/RM2D.h): Transform 2D。RegisterRuntime が game production の
// kScriptApiPoint に出す。

#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_2d {

const game_production::ScriptApiPart& ScriptTransform2DPart();
const game_production::ScriptApiPart& ScriptTransform2DPart2();  // (とワールド: RmTransform2DApi2)
// Entity::SetParent(p, keepWorld): `newParent` の下でも今の場所に残るように Transform 2D を変える。
void KeepWorld2D(game_production::Scene& scene, int entity, int newParent);
// エンティティが画面のどこにいるか (親の Transform 2D も含める。z = 0、画面の奥向き):
// 2D の空間の game production の PlaceInfo (2D のゲームの音はそこから聞こえる)。
bool Pose2D(const game_production::Scene& scene, int entity, game_production::Pose& out);

}  // namespace rm::game_x_2d
