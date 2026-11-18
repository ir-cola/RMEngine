#pragma once
// ゲーム制作 × パーティクル - the component パーティクル: plays an effect (.rmparticle) where its entity is.
// In the editor the effect of a selected entity plays in the scene view without Play (to be looked at);
// in Play and the exported game every one plays (ParticleDraw.h).

#include "base/Ref.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include <cstdint>

namespace rm::game_x_particles {

RM_STRUCT(Label="パーティクル")
struct ParticleSystem {
    RM_PROPERTY(Edit, Label="エフェクト", Tooltip="パーティクルのエフェクト (.rmparticle)。空なら、見本の光が出ます", AssetKind=".rmparticle")
    AssetRef effect;
    RM_PROPERTY(Edit, Label="始めから出す", Tooltip="外すと、ゲームでは出しません (止まっているときのシーンビューでは、選ぶと出ます)") bool playOnStart = true;
    RM_PROPERTY(Edit, Label="速さ", Tooltip="時間の進み方の倍率 (2 で倍の速さ)", Min=0, Max=10, Step=0.05) float speed = 1;
    // What scripts asked (RMParticles.h's rm::Particles) - the copy that plays only. The drawer does each
    // counted request once (it keeps how many it did).
    RM_TRANSIENT() int scriptPlay = 0;     // 0: as 始めから出す; 1: played by a script; 2: stopped (what is out dies away)
    RM_TRANSIENT() bool paused = false;    // held still
    RM_TRANSIENT() uint32_t restarts = 0;  // Play from the start / Stop with clearing: from nothing again
    RM_TRANSIENT() uint32_t emitted = 0;   // particles asked at once (Emit), counted up
};

// It lets out particles while the game plays (始めから出す, or what a script said).
inline bool Emitting(const ParticleSystem& ps) { return ps.scriptPlay == 1 || (ps.scriptPlay == 0 && ps.playOnStart); }

// The scripts' part (RMParticles.h): play, stop, pause, emit, the speed.
const game_production::ScriptApiPart& ScriptParticlesPart();

}  // namespace rm::game_x_particles
