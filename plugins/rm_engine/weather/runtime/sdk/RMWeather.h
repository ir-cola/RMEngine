// RMWeather.h - スクリプトから使うシーンの天候 (プラグイン 天候 が与える): すぐに変えるか、
// 数秒かけてなめらかに変える。量は 0 .. 100、インスペクターに出るとおり。
//
//   #include "RMWeather.h"
//   rm::Sky::SetWeather(rm::Weather::Rain, 10.0f);    // 雲が出て、そのあと雨。10 秒かけて
//   rm::Sky::SetPrecipitation(100.0f, 5.0f);           // 嵐の雨に。5 秒かけて
//   rm::Sky::SetSnowing(true);                         // 雨が弱まり、雪に変わる (約 5 秒)
//   rm::Sky::SetFog(60.0f, 10.0f);                     // 濃い霧に。10 秒かけて
//   rm::Sky::SetWind(90.0f, 12.0f, 3.0f);              // 左 (-X) へ、12 m/s、3 秒かけて
//   float falling = rm::Sky::RainNow();                // 今この瞬間の雨の量 (0 .. 100)
//
// (空の 時刻 は RM3D.h の rm::Sky::SetHour。) プラグインがない、または 天候 のないシーンでは、
// 何も起きず、ゲッターは 0 を返す。
#pragma once

#include "RM.h"
#include "RMWeatherAbi.h"

namespace rm {

// できあいの天候 (インスペクターの 天候 → テンプレート): それぞれ、雲の量、降水量、雪にする、霧の量、
// 雷、風の強さを決める。
enum class Weather {
    Clear = 0, Fair, Cloudy, Foggy, Sprinkle, Drizzle, Rain, Downpour, Storm, LightSnow, Snow, HeavySnow, Blizzard,
};
// seconds: 変化にかかる時間 (0: すぐに) - 緩急つきで、その瞬間の天候から。
namespace Sky {
namespace detail {
inline const RmWeatherApi* ApiWeather() {
    static const RmWeatherApi* api = Part<RmWeatherApi>(RM_PART_WEATHER, RM_PART_WEATHER_VERSION);
    return api;
}
inline void SetWeatherValue(int which, float value, float seconds) {
    if (const RmWeatherApi* s = ApiWeather()) s->set(Api()->ctx, which, value, seconds);
}
inline float WeatherValue(int which, int now, float fallback) {
    float v = fallback;
    if (const RmWeatherApi* s = ApiWeather()) s->get(Api()->ctx, which, now, &v);
    return v;
}
}  // namespace detail

// シーンに 天候 がある (そしてこのエンジンにプラグインがある)。
inline bool HasWeather() { return detail::ApiWeather() && detail::ApiWeather()->weather(Api()->ctx) >= 0; }
inline void SetWeather(Weather weather, float seconds = 0) {
    if (const RmWeatherApi* s = detail::ApiWeather()) s->setWeather(Api()->ctx, static_cast<int>(weather), seconds);
}
// 雲の量 (0 .. 100: few - fair, many - cloudy; rain darkens them).
inline void SetClouds(float amount, float seconds = 0) { detail::SetWeatherValue(0, amount, seconds); }
inline float Clouds() { return detail::WeatherValue(0, 0, 0); }
// 降水量 (0 .. 100): 雨、または 雪にする が入っている間は雪 - 両方にはならない。
inline void SetPrecipitation(float amount, float seconds = 0) { detail::SetWeatherValue(1, amount, seconds); }
inline float Precipitation() { return detail::WeatherValue(1, 0, 0); }
// 雪にする: 雨が弱まって霧雨になり、約 5 秒かけて雪に変わる (または戻る)。
inline void SetSnowing(bool on) { detail::SetWeatherValue(5, on ? 1.0f : 0.0f, 0); }
inline bool Snowing() { return detail::WeatherValue(5, 0, 0) != 0; }
// 霧の量 (0 .. 100: 100 で数 m 先まで見える)。
inline void SetFog(float amount, float seconds = 0) { detail::SetWeatherValue(2, amount, seconds); }
inline float Fog() { return detail::WeatherValue(2, 0, 0); }
// 今この瞬間に降っているもの (0 .. 100)。
inline float RainNow() { return detail::WeatherValue(1, 1, 0); }
inline float SnowNow() { return detail::WeatherValue(9, 1, 0); }
// 積もる高さ (cm、0 .. 5) と、今の雪の深さ (cm)。
inline void SetSnowDepth(float cm) { detail::SetWeatherValue(8, cm, 0); }
inline float SnowLying() { return detail::WeatherValue(8, 1, 0); }
// 風: 吹いていく先 (度、真上から見る: 0 は正面 -Z、90 は左 -X) と強さ (m/s)。雲はそれで
// 流れ、雨と雪は斜めになる。
inline void SetWind(float directionDegrees, float strength, float seconds = 0) {
    detail::SetWeatherValue(3, directionDegrees, seconds);
    detail::SetWeatherValue(4, strength, seconds);
}
inline float WindDirection() { return detail::WeatherValue(3, 0, 0); }
inline float WindStrength() { return detail::WeatherValue(4, 0, 0); }
// 雷: at once.
inline void SetLightning(bool on) { detail::SetWeatherValue(6, on ? 1.0f : 0.0f, 0); }
// 濡れ: 雨が届くものが今どれだけ濡れているか (0 .. 1。雨の間は濡れ、そのあと乾く)。
inline float Wetness() { return detail::WeatherValue(7, 1, 0); }
// フェードが進んでいる (0 .. 1 でどこまで来たか。1: なし)。
inline float Fading() {
    const RmWeatherApi* s = detail::ApiWeather();
    return s ? s->fading(Api()->ctx) : 1.0f;
}
}  // namespace Sky

}  // namespace rm
