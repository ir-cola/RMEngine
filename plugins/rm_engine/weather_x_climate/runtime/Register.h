#pragma once
// コネクター 天候 × 気候 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは RM_RUNTIME_ENTRY から呼ぶ):
// 気候の気温と湿り気から、天候の「場所ごとの降り方」(WeatherPlace.h) に答える。設計: docs/CLIMATE.md。
//   - 雨粒と雪の 1 粒ずつ: カメラのまわりの格子 (天候の口 rm_engine.weather.place)
//   - Play の間の今の天候 (霧など): カメラのいる所を、数秒かけて Weather::placeSnow / placeMore に
//     (システム「天候 × 気候」: 天候のシステムの前)
//   - Play の間の濡れと積雪: カメラのまわりの地図のマスごと (weather::PlaceCellsFor。同じシステム)
//   - 気候の地図に描いた雨雲: その場所だけ降る量 (WeatherPlace::local。シーンの降水量が 0 でも降る。
//     雨か雪かはその場所の気温)。シーン全体の雨は天候の「降水量」(インスペクター)
//   - 気候の地図に塗った雲と雨雲: Play の間、カメラのいる所の雲 (Weather::placeClouds / placeCloudShare) と、
//     空に描く雲の地図 (Weather::cloudCells / cloudShares)。雨雲の所は厚い雲 (RainCloudsOf)

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
// 気温・湿り気・描いた雨雲 -> 降り方 (雪の割合 = SnowShareOf、倍率 = MoreOf、
// その場所だけ降る量 = 雨雲の割合 x 降る量)。
weather::WeatherPlace PlaceOf(const climate::ClimateHere& c);
// 雨雲の降る量 (%) -> そこの空の雲の量 (%): 70 % (弱い雨) 〜 100 % (嵐)。
float RainCloudsOf(float rain);
// 塗った雲 (割合 `weight`・量 `amount`) に、雨雲 (割合 `rainWeight`・降る量 `rainAmount`) を重ねた雲:
// 割合 = 1 - (1 - weight)(1 - rainWeight)、量 = 割合で重みをつけた平均 (雨雲は RainCloudsOf)。
void MixRainClouds(float& weight, float& amount, float rainWeight, float rainAmount);
// シーンのどこかで、その場所だけ降る量の一番多いもの (0 .. 1: 描いた雨雲の降る量)。
float MostLocalOf(const game_production::Scene& scene);
// `value` を `to` へ `dt` 秒ぶん近づける (約 2 秒で 63 %)。
void ApproachValue(float& value, float to, float dt);
// カメラのいる所の降り方へ、`dt` 秒ぶん近づける (ApproachValue。まだ無い (< 0): すぐにその値)。
void Approach(float& placeSnow, float& placeMore, float& placeLocal, const weather::WeatherPlace& to, float dt);
// カメラのいる所の塗った雲 (割合 `weight`・量 `amount`) へ近づける (まだ無い (placeClouds < 0): すぐに)。
// 量は、塗った所にいるときだけ動かす (割合 0 の所の量は意味が無い)。
void ApproachClouds(float& placeClouds, float& placeShare, float weight, float amount, float dt);
// `w` の雲の地図 (cloudCells / cloudShares / cloudFirstX / Z) を、カメラ `eye` のまわりで `field` から書く。
// 雲も雨雲も描いていない地図: 空にする。
void CloudCellsFor(weather::Weather& w, const climate::ClimateField& field, Vec3 eye);

}  // namespace rm::weather_x_climate
