// RMClimate.h - スクリプトから読む場所ごとの気候 (プラグイン 気候 が与える): 気温 (℃) と湿り気 (%)。
// 気候エリアの中ほど、そのエリアの値に近い (境目ではなめらかに変わる)。3D では高い所ほど冷える。
//
//   #include "RMClimate.h"
//   float t = rm::Climate::TemperatureAt(rm::Transform3D(self).WorldPosition());  // 今いる所の気温 (RM3D.h)
//   if (t < 0.0f) cold += dt;                                 // 寒い所にいる間、体が冷える
//   float h = rm::Climate::HumidityAt2D({x, y});              // 2D のゲームの場所の湿り気
//   rm::Climate::SetTemperature(-10.0f);                      // エリアの外の気温を変える (すぐに)
//
// プラグインが無いときは、気温 15・湿り気 50 を返す (設定を変える関数は何もしない)。
#pragma once

#include "RM.h"
#include "RMClimateAbi.h"

namespace rm {
namespace Climate {
namespace detail {
inline const RmClimateApi* ApiClimate() {
    static const RmClimateApi* api = Part<RmClimateApi>(RM_PART_CLIMATE, RM_PART_CLIMATE_VERSION);
    return api;
}
inline float At(float x, float y, float z, int flat, bool humidity) {
    float t = 15.0f, h = 50.0f;
    const float pos[3] = {x, y, z};
    if (const RmClimateApi* c = ApiClimate()) c->at(Api()->ctx, pos, flat, &t, &h);
    return humidity ? h : t;
}
inline void SetValue(int which, float value) {
    if (const RmClimateApi* c = ApiClimate()) c->set(Api()->ctx, which, value);
}
inline float Value(int which, float fallback) {
    float v = fallback;
    if (const RmClimateApi* c = ApiClimate()) c->get(Api()->ctx, which, &v);
    return v;
}
}  // namespace detail

// 3D の場所の気温 (℃) と湿り気 (%)。
inline float TemperatureAt(Vec3 p) { return detail::At(p.x, p.y, p.z, 0, false); }
inline float HumidityAt(Vec3 p) { return detail::At(p.x, p.y, p.z, 0, true); }
// 2D の場所の気温 (℃) と湿り気 (%)。
inline float TemperatureAt2D(Vec2 p) { return detail::At(p.x, p.y, 0, 1, false); }
inline float HumidityAt2D(Vec2 p) { return detail::At(p.x, p.y, 0, 1, true); }
// シーンの設定 (気候エリアの外の気候): すぐに変わる。
inline void SetTemperature(float celsius) { detail::SetValue(0, celsius); }
inline float Temperature() { return detail::Value(0, 15.0f); }
inline void SetHumidity(float percent) { detail::SetValue(1, percent); }
inline float Humidity() { return detail::Value(1, 50.0f); }
// 高い所ほど冷える量 (℃ / 100 m。3D)。
inline void SetLapse(float celsiusPer100m) { detail::SetValue(2, celsiusPer100m); }
inline float Lapse() { return detail::Value(2, 0.6f); }
}  // namespace Climate
}  // namespace rm
