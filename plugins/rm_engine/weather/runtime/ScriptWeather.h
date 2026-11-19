#pragma once
// The weather part of the scripts' API (sdk/RMWeather.h: rm::Sky::SetWeather / SetRain / SetWind ...):
// the scene's 天候, at once or faded (Weather.h's BeginFade). Offered at kScriptApiPoint by RegisterRuntime.

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::weather {

const game_production::ScriptApiPart& ScriptWeatherPart();

}  // namespace rm::weather
