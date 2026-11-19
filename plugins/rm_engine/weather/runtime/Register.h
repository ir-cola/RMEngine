#pragma once
// The runtime part of 天候 (listed in plugins/RuntimePlugins.cpp; the exported game calls it by
// RM_RUNTIME_ENTRY): the scene setting 天候 (Weather.h), the system that moves it on while the game
// plays, what it tells the sky (ゲーム制作 × 3D's SkyWeather), its drawing into the 3D pictures
// (WeatherDraw.h) and the scripts' part (sdk/RMWeather.h).

#include "core/RuntimeRegistry.h"

namespace rm::weather {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::weather
