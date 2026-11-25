#pragma once
// コネクター 天候 × 気候 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは RM_RUNTIME_ENTRY から呼ぶ):
// 気候の気温と湿り気から、天候の「場所ごとの降り方」(WeatherPlace.h) に答える。設計: docs/CLIMATE.md。
//   - 雨粒と雪の 1 粒ずつ: カメラのまわりの格子 (天候の口 rm_engine.weather.place)
//   - Play の間の今の天候 (霧など): カメラのいる所を、数秒かけて Weather::placeSnow / placeMore に
//     (システム「天候 × 気候」: 天候のシステムの前)
//   - Play の間の濡れと積雪: カメラのまわりの地図のマスごと (weather::PlaceCellsFor。同じシステム)
//   - 気候の地図に塗った雲: 雨と雪は雲のある所だけ (降水量の倍率)。Play の間、カメラのいる所の雲
//     (Weather::placeClouds / placeCloudShare) と、空に描く雲の地図 (Weather::cloudCells / cloudShares)

#include "core/RuntimeRegistry.h"
#include "rm_engine/climate/runtime/Climate.h"
#include "rm_engine/weather/runtime/Weather.h"
#include "rm_engine/weather/runtime/WeatherPlace.h"

namespace rm::weather_x_climate {

void RegisterRuntime(RuntimeRegistry& registry);

// 気温 (℃) -> 降るもののうち雪の割合: +2 ℃ 以上は 0 (雨)、-1 ℃ 以下は 1 (雪)、間はなめらかに (みぞれ)。
float SnowShareOf(float celsius);
// 湿り気 (%) -> 降水量の倍率: 0 % で 0 (降らない)、50 % で 1、100 % で kPlaceMostMore (1.5)。
float MoreOf(float humidity);
// 雲の量 (%) -> 雨と雪の降る倍率: 30 % 以下で 0 〜 60 % 以上で 1 (なめらかに)。
float RainUnderClouds(float clouds);
// 雲を塗った割合 `weight` と、そこの雲の量 `amount` (%) の所の降水量の倍率:
// (1 - weight) + weight x RainUnderClouds(amount)。塗っていない所 (weight 0) は 1。
float CloudRainOf(float weight, float amount);
// 気温・湿り気・塗った雲 -> 降り方 (倍率 = MoreOf x CloudRainOf)。
weather::WeatherPlace PlaceOf(const climate::ClimateHere& c);
// `value` を `to` へ `dt` 秒ぶん近づける (約 2 秒で 63 %)。
void ApproachValue(float& value, float to, float dt);
// カメラのいる所の降り方へ、`dt` 秒ぶん近づける (ApproachValue。まだ無い (< 0): すぐにその値)。
void Approach(float& placeSnow, float& placeMore, const weather::WeatherPlace& to, float dt);
// カメラのいる所の塗った雲 (割合 `weight`・量 `amount`) へ近づける (まだ無い (placeClouds < 0): すぐに)。
// 量は、塗った所にいるときだけ動かす (割合 0 の所の量は意味が無い)。
void ApproachClouds(float& placeClouds, float& placeShare, float weight, float amount, float dt);
// `w` の雲の地図 (cloudCells / cloudShares / cloudFirstX / Z) を、カメラ `eye` のまわりで `field` から書く。
// 雲を塗っていない地図: 空にする。
void CloudCellsFor(weather::Weather& w, const climate::ClimateField& field, Vec3 eye);

}  // namespace rm::weather_x_climate
