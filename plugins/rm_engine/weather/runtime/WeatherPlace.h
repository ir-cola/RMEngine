#pragma once
// 天候 - 場所ごとの降り方の口: 降るもののうち、その場所でどれだけ雪になるか (残りは雨) と、降る量の倍率。
// 天候はここに答える者を知らない (今はつなぎ「天候 × 気候」が気温と湿り気から答える。docs/CLIMATE.md)。
//
//   static const weather::WeatherPlaceInfo kPlace{&Grid};
//   registry.Contribute(weather::kWeatherPlacePoint, kPlace);
//
// 答える者がいるとき:
//   - 雨粒と雪の 1 粒ずつが、自分の場所の割合で出るか消えるかを決める (境目では両方が混ざる: みぞれ)。
//     割合はカメラのまわりの格子で渡す (WeatherDraw)
//   - Play の間の「今の天候」(霧・濡れ・積雪) は、答える者が Weather::placeSnow / placeMore に書く
//     (カメラのいる所。数秒かけて近づける)。そのあいだ「雪にする」は使わない

#include "base/Math.h"
#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"

namespace rm::weather {

// RuntimeRegistry の口: 項目は WeatherPlaceInfo (static)。
inline constexpr char kWeatherPlacePoint[] = "rm_engine.weather.place";

struct WeatherPlace {
    float snow = 0;  // 0: 雨 .. 1: 雪
    float more = 1;  // 降水量の倍率 (0: 降らない .. 1: そのまま .. 1.5 ほど: 多い)
};

struct WeatherPlaceInfo {
    // `center` を真ん中に、真上から見て `step` m おきに `count` x `count` 点 (高さは center.y) の降り方を
    // `out` に (行が z、列が x。z の小さい行から)。
    void (*grid)(const game_production::Scene& scene, Vec3 center, float step, int count, WeatherPlace* out) = nullptr;
};

// 降水量の倍率の一番大きいもの (答える者はこれまでにする): 粒はこの倍率のぶん用意して、場所で減らす。
inline constexpr float kPlaceMostMore = 1.5f;
// 粒の格子の点の数 (一辺)。箱の一辺をこの数 - 1 で割った間隔で並べる。
inline constexpr int kPlaceGrid = 7;

}  // namespace rm::weather
