#pragma once
// コネクター 天候 × 気候 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは RM_RUNTIME_ENTRY から呼ぶ):
// 気候の気温と湿り気から、天候の「場所ごとの降り方」(WeatherPlace.h) に答える。設計: docs/CLIMATE.md。
//   - 雨粒と雪の 1 粒ずつ: カメラのまわりの格子 (天候の口 rm_engine.weather.place)
//   - Play の間の今の天候 (霧など): カメラのいる所を、数秒かけて Weather::placeSnow / placeMore に
//     (システム「天候 × 気候」: 天候のシステムの前)
//   - Play の間の濡れと積雪: カメラのまわりの地図のマスごと (weather::PlaceCellsFor。同じシステム)
//   - 気候の地図に描いた雨雲: その場所だけ降る量 (WeatherPlace::local。シーンの降水量が 0 でも降る。
//     雨か雪かはその場所の気温)。シーン全体の雨は天候の「降水量」(インスペクター)
//   - 気候の地図に塗った雲: Play の間、カメラのいる所の雲 (Weather::placeClouds / placeCloudShare)
//   - 空に描く雲の地図 (天候の口の clouds: 空を描くたび、エディターでも): 塗った雲と、描いた雨雲の降る量。
//     天候は降る所の上に、降る量に比例した雨雲を描く (雨雲の無い所には降らない)

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
// シーンのどこかで、その場所だけ降る量の一番多いもの (0 .. 1: 描いた雨雲の降る量)。
float MostLocalOf(const game_production::Scene& scene);
// `value` を `to` へ `dt` 秒ぶん近づける (約 2 秒で 63 %)。
void ApproachValue(float& value, float to, float dt);
// カメラのいる所の降り方へ、`dt` 秒ぶん近づける (ApproachValue。まだ無い (< 0): すぐにその値)。
void Approach(float& placeSnow, float& placeMore, float& placeLocal, const weather::WeatherPlace& to, float dt);
// カメラのいる所の塗った雲 (割合 `weight`・量 `amount`) へ近づける (まだ無い (placeClouds < 0): すぐに)。
// 量は、塗った所にいるときだけ動かす (割合 0 の所の量は意味が無い)。
void ApproachClouds(float& placeClouds, float& placeShare, float weight, float amount, float dt);
// 描いた雲と雨雲を運んだ風 (x, z。m): 天候が Play の間に足し合わせた風 (Weather::driftX / Z)。天候が無い: 0。
// 描いた雲と雨雲は、描いた所から風下へこの分だけ動いた所にある (強い風ほど速く動く)。
Vec2 CloudDriftOf(const game_production::Scene& scene);
// 場所 `xz` の、描いた雲と雨雲 (風で運ばれた分を戻した所を読む)。雨雲は点で読む (描いた境目の外へにじませない)。
void DriftedClouds(const climate::ClimateField& field, Vec2 xz, Vec2 drift, climate::ClimateHere& here);
// カメラ `eye` のまわりの雲の地図 (weather::PlaceClouds) を `field` から書く: マスの真ん中のまわり 1 マス
// 四方の、塗った雲の割合と量・その場所だけ降る量 (雨雲の割合 x 降る量)。風で運ばれた分 `drift` だけずらす。
// 雲も雨雲も描いていない地図: 空にする。
void CloudMapFor(weather::PlaceClouds& out, const climate::ClimateField& field, Vec3 eye, Vec2 drift = {0, 0});

}  // namespace rm::weather_x_climate
