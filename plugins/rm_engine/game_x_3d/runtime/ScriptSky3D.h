#pragma once
// The sky part of the scripts' API (sdk/RM3D.h: rm::Sky): the scene's 空 - its weather, amounts and
// wind, at once or faded (Sky.h's BeginFade). Offered at kScriptApiPoint by RegisterRuntime.

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_3d {

const game_production::ScriptApiPart& ScriptSky3DPart();

}  // namespace rm::game_x_3d
