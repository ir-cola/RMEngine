#pragma once
// ゲーム制作 × パーティクル - the component パーティクル: plays an effect (.rmparticle) where its entity is.
// In the editor the effect of a selected entity plays in the scene view without Play (to be looked at);
// in Play and the exported game every one plays (ParticleDraw.h).

#include "base/Ref.h"
#include "core/reflect/Reflect.h"

namespace rm::game_x_particles {

RM_STRUCT(Label="パーティクル")
struct ParticleSystem {
    RM_PROPERTY(Edit, Label="エフェクト", Tooltip="パーティクルのエフェクト (.rmparticle)。空なら、見本の光が出ます", AssetKind=".rmparticle")
    AssetRef effect;
    RM_PROPERTY(Edit, Label="始めから出す", Tooltip="外すと、ゲームでは出しません (止まっているときのシーンビューでは、選ぶと出ます)") bool playOnStart = true;
    RM_PROPERTY(Edit, Label="速さ", Tooltip="時間の進み方の倍率 (2 で倍の速さ)", Min=0, Max=10, Step=0.05) float speed = 1;
};

}  // namespace rm::game_x_particles
