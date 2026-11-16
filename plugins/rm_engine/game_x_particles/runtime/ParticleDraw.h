#pragma once
// ゲーム制作 × パーティクル - the particles in a 3D picture (ゲーム制作 × 3D's WorldDrawer): one
// EffectPlayer per entity with パーティクル, moved on by the picture's time and drawn into it.
//
//   playing (Play, the exported game)   every パーティクル with 始めから出す
//   not playing (the editor)            the selected ones (to be looked at), from the start when selected
//
// An effect is read from its asset once and looked at again now and then (a file saved meanwhile -
// the パーティクル tab, another program - plays as it is now). A problem (an effect that cannot be read, a
// shader that does not compile) is told once in the console.

#include "rm_engine/game_x_3d/runtime/WorldDraw.h"
#include "rm_engine/particles/runtime/ParticleShader.h"

#include <memory>

namespace rm::game_x_particles {

std::unique_ptr<game_x_3d::WorldDrawer> CreateParticleDraw();

// The camera of a 3D picture as the particles' shaders take it.
particles::ViewConstants ViewOf(const space3d::ExtraDrawContext& c);

}  // namespace rm::game_x_particles
