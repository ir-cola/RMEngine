#pragma once
// The 2D part of the scripts' API (sdk/RM2D.h): Transform 2D. Offered at game production's
// kScriptApiPoint by RegisterRuntime.

#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_2d {

const game_production::ScriptApiPart& ScriptTransform2DPart();
// Where an entity is on the screen (its parents' Transform 2D too; z = 0, facing into the screen):
// game production's PlaceInfo of the 2D space (the sounds of a 2D game are heard from there).
bool Pose2D(const game_production::Scene& scene, int entity, game_production::Pose& out);

}  // namespace rm::game_x_2d
