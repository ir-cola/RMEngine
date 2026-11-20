#pragma once
// スクリプト API の 3D の部分 (sdk/RM3D.h): Transform。RegisterRuntime がゲーム制作の
// kScriptApiPoint に出す。

#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_3d {

const game_production::ScriptApiPart& ScriptTransform3DPart();
const game_production::ScriptApiPart& ScriptTransform3DPart2();  // (とワールド: RmTransform3DApi2)
// Entity::SetParent(p, keepWorld): `newParent` の下でもその場所にいるように Transform を変える。
void KeepWorld3D(game_production::Scene& scene, int entity, int newParent);
// エンティティのワールドでの位置 (親の Transform も含める) と、見る向き (-Z、カメラと同じ):
// 3D の空間のゲーム制作の PlaceInfo (3D のゲームの音は、そこから聞こえる)。
bool Pose3D(const game_production::Scene& scene, int entity, game_production::Pose& out);

}  // namespace rm::game_x_3d
