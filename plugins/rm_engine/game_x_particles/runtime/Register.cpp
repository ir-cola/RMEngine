#include "rm_engine/game_x_particles/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_x_3d/runtime/WorldDraw.h"
#include "rm_engine/game_x_particles/runtime/ParticleDraw.h"
#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"

namespace rm::game_x_particles {

void RegisterRuntime(RuntimeRegistry& registry) {
    // パーティクル (several on one entity are several effects).
    static const game_production::ComponentInfo kSystem{.type = &reflect::TypeOf<ParticleSystem>, .multiple = true, .folder = "エフェクト"};
    registry.Contribute(game_production::kComponentPoint, kSystem);
    // Drawn into the 3D pictures: the scene view, the game view and the exported game.
    static const game_x_3d::WorldDrawInfo kDraw{&CreateParticleDraw};
    registry.Contribute(game_x_3d::kWorldDrawPoint, kDraw);
}

}  // namespace rm::game_x_particles

RM_RUNTIME_ENTRY(rm_engine, game_x_particles, rm::game_x_particles::RegisterRuntime)
