#pragma once
// コネクター 天候 × 気候 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは RM_RUNTIME_ENTRY から呼ぶ):
// 気候の気温と湿り気から、天候の「場所ごとの降り方」(WeatherPlace.h) に答える。設計: docs/CLIMATE.md。
//   - 雨粒と雪の 1 粒ずつ: カメラのまわりの格子 (天候の口 rm_engine.weather.place)
//   - Play の間の今の天候 (霧・濡れ・積雪): カメラのいる所を、数秒かけて Weather::placeSnow / placeMore に
//     (システム「天候 × 気候」: 天候のシステムの前)

#include "core/RuntimeRegistry.h"
#include "rm_engine/climate/runtime/Climate.h"
#include "rm_engine/weather/runtime/WeatherPlace.h"

namespace rm::weather_x_climate {

void RegisterRuntime(RuntimeRegistry& registry);

// 気温 (℃) -> 降るもののうち雪の割合: +2 ℃ 以上は 0 (雨)、-1 ℃ 以下は 1 (雪)、間はなめらかに (みぞれ)。
float SnowShareOf(float celsius);
// 湿り気 (%) -> 降水量の倍率: 0 % で 0 (降らない)、50 % で 1、100 % で kPlaceMostMore (1.5)。
float MoreOf(float humidity);
weather::WeatherPlace PlaceOf(const climate::ClimateHere& c);
// カメラのいる所の降り方へ、`dt` 秒ぶん近づける (約 2 秒で 63 %。まだ無い (< 0): すぐにその値)。
void Approach(float& placeSnow, float& placeMore, const weather::WeatherPlace& to, float dt);

}  // namespace rm::weather_x_climate
