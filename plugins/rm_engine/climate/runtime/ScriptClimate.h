#pragma once
// スクリプト API の気候の部分 (sdk/RMClimate.h: rm::Climate::TemperatureAt / HumidityAt ...)。
// RegisterRuntime が kScriptApiPoint で提供する。

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::climate {

const game_production::ScriptApiPart& ScriptClimatePart();

}  // namespace rm::climate
