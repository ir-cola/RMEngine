#include "rm_engine/game_x_particles/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_x_3d/runtime/WorldDraw.h"
#include "rm_engine/game_x_particles/runtime/ParticleDraw.h"
#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"

namespace rm::game_x_particles {

void RegisterRuntime(RuntimeRegistry& registry) {
    // パーティクル (1 つのエンティティに複数あれば、複数のエフェクト)。
    static const game_production::ComponentInfo kSystem{.type = &reflect::TypeOf<ParticleSystem>, .multiple = true, .folder = "エフェクト"};
    registry.Contribute(game_production::kComponentPoint, kSystem);
    // 3D の絵に描く: シーンビュー、ゲームビュー、書き出したゲーム。
    static const game_x_3d::WorldDrawInfo kDraw{&CreateParticleDraw};
    registry.Contribute(game_x_3d::kWorldDrawPoint, kDraw);
    // スクリプト: 再生 / 停止 / 出す (sdk/RMParticles.h)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptParticlesPart());
}

}  // namespace rm::game_x_particles

RM_RUNTIME_ENTRY(rm_engine, game_x_particles, rm::game_x_particles::RegisterRuntime)
