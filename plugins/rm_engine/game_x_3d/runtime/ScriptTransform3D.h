#pragma once
// The 3D part of the scripts' API (sdk/RM3D.h): Transform. Offered at game production's
// kScriptApiPoint by RegisterRuntime.

#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_3d {

const game_production::ScriptApiPart& ScriptTransform3DPart();
// Where an entity is in the world (its parents' Transforms too) and which way it looks (-Z, as a camera):
// game production's PlaceInfo of the 3D space (the sounds of a 3D game are heard from there).
bool Pose3D(const game_production::Scene& scene, int entity, game_production::Pose& out);

}  // namespace rm::game_x_3d
