#pragma once
// The game UI's part of the scripts' API (sdk/RMUI.h): texts, images, bars, buttons and the UI windows
// of the scene that plays. Offered at game production's kScriptApiPoint.

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_ui {

const game_production::ScriptApiPart& ScriptUiPart();

}  // namespace rm::game_ui
