#pragma once
// ゲーム制作 × パーティクル - 部品 パーティクル: エンティティのある所でエフェクト (.rmparticle) を再生する。
// エディターでは、選んだエンティティのエフェクトは Play しなくてもシーンビューで再生される (見るため)。
// Play と書き出したゲームでは、すべて再生される (ParticleDraw.h)。

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
    // スクリプトが頼んだこと (RMParticles.h の rm::Particles) - 再生するコピーだけが持つ。描き手は
    // 数える頼み事を 1 度ずつ処理する (何回処理したかは描き手が覚える)。
    RM_TRANSIENT() int scriptPlay = 0;     // 0: 始めから出す の通り; 1: スクリプトが再生; 2: 止めた (出ているものは消えていく)
    RM_TRANSIENT() bool paused = false;    // 止めてある
    RM_TRANSIENT() uint32_t restarts = 0;  // 最初から Play / 消して Stop: また 0 から
    RM_TRANSIENT() uint32_t emitted = 0;   // 一度に頼まれたパーティクル (Emit)、足し上げる
};

// ゲームが動いている間パーティクルを出す (始めから出す、またはスクリプトが言ったこと)。
inline bool Emitting(const ParticleSystem& ps) { return ps.scriptPlay == 1 || (ps.scriptPlay == 0 && ps.playOnStart); }

// スクリプト向けの部分 (RMParticles.h): 再生、停止、一時停止、出す、速さ。
const game_production::ScriptApiPart& ScriptParticlesPart();

}  // namespace rm::game_x_particles
