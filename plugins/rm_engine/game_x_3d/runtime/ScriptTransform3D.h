#pragma once
// スクリプト API の 3D の部分 (sdk/RM3D.h): Transform。RegisterRuntime がゲーム制作の
// kScriptApiPoint に出す。

#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_3d {

const game_production::ScriptApiPart& ScriptTransform3DPart();
const game_production::ScriptApiPart& ScriptTransform3DPart2();  // (とワールド: RmTransform3DApi2)
// Entity::SetParent(p, keepWorld): `newParent` の下でもその場所にいるように Transform を変える。
void KeepWorld3D(game_production::Scene& scene, int entity, int newParent);
// エンティティのワールドでの位置 (親の Transform も含める) と、見る向き (-Z、カメラと同じ):
// 3D の空間のゲーム制作の PlaceInfo (3D のゲームの音は、そこから聞こえる)。
bool Pose3D(const game_production::Scene& scene, int entity, game_production::Pose& out);
// エンティティのワールド行列: 一番上から `entity` まで (self = false: その親まで) の Transform を掛けたもの
// (Transform の無いものは飛ばす。1 つも無ければ単位行列)。描くときと同じ (骨に付ける が使う)。
space3d::Mat4 WorldMatrixOf(const game_production::Scene& scene, int entity, bool self = true);

}  // namespace rm::game_x_3d
