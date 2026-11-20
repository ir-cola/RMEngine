#pragma once
// スクリプト API の天候の部分 (sdk/RMWeather.h: rm::Sky::SetWeather / SetRain / SetWind ...):
// シーンの 天候 を、すぐに、またはフェードで (Weather.h の BeginFade)。RegisterRuntime が
// kScriptApiPoint で提供する。

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::weather {

const game_production::ScriptApiPart& ScriptWeatherPart();

}  // namespace rm::weather
