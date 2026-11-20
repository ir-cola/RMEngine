// RMWeather.h - the scene's weather for scripts (the plugin 天候 gives it): change it at once or faded
// over some seconds. Amounts are 0 .. 100, as the inspector shows them.
//
//   #include "RMWeather.h"
//   rm::Sky::SetWeather(rm::Weather::Rain, 10.0f);    // clouds come, then the rain, over 10 seconds
//   rm::Sky::SetPrecipitation(100.0f, 5.0f);           // a storm's rain, over 5 seconds
//   rm::Sky::SetSnowing(true);                         // the rain weakens, then turns to snow (about 5 seconds)
//   rm::Sky::SetFog(60.0f, 10.0f);                     // a thick fog, over 10 seconds
//   rm::Sky::SetWind(90.0f, 12.0f, 3.0f);              // to the left (-X), 12 m/s, over 3 seconds
//   float falling = rm::Sky::RainNow();                // how much rain falls this moment (0 .. 100)
//
// (The sky's 時刻 is RM3D.h's rm::Sky::SetHour.) Without the plugin, or a scene without 天候: nothing
// happens and the getters give zeros.
#pragma once

#include "RM.h"
#include "RMWeatherAbi.h"

namespace rm {

// The ready-made weathers (the inspector's 天候 → テンプレート): each sets 雲の量, 降水量, 雪にする, 霧の量,
// 雷 and the wind's strength.
enum class Weather {
    Clear = 0, Fair, Cloudy, Foggy, Sprinkle, Drizzle, Rain, Downpour, Storm, LightSnow, Snow, HeavySnow, Blizzard,
};
// seconds: how long the change takes (0: at once) - eased in and out, from the weather of the moment.
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

// The scene has 天候 (and this engine has the plugin).
inline bool HasWeather() { return detail::ApiWeather() && detail::ApiWeather()->weather(Api()->ctx) >= 0; }
inline void SetWeather(Weather weather, float seconds = 0) {
    if (const RmWeatherApi* s = detail::ApiWeather()) s->setWeather(Api()->ctx, static_cast<int>(weather), seconds);
}
// 雲の量 (0 .. 100: few - fair, many - cloudy; rain darkens them).
inline void SetClouds(float amount, float seconds = 0) { detail::SetWeatherValue(0, amount, seconds); }
inline float Clouds() { return detail::WeatherValue(0, 0, 0); }
// 降水量 (0 .. 100): rain, or snow while 雪にする is on - never both.
inline void SetPrecipitation(float amount, float seconds = 0) { detail::SetWeatherValue(1, amount, seconds); }
inline float Precipitation() { return detail::WeatherValue(1, 0, 0); }
// 雪にする: the rain weakens to a drizzle, then turns to snow (or back) over about 5 seconds.
inline void SetSnowing(bool on) { detail::SetWeatherValue(5, on ? 1.0f : 0.0f, 0); }
inline bool Snowing() { return detail::WeatherValue(5, 0, 0) != 0; }
// 霧の量 (0 .. 100: at 100 a few metres seen).
inline void SetFog(float amount, float seconds = 0) { detail::SetWeatherValue(2, amount, seconds); }
inline float Fog() { return detail::WeatherValue(2, 0, 0); }
// What falls this moment (0 .. 100).
inline float RainNow() { return detail::WeatherValue(1, 1, 0); }
inline float SnowNow() { return detail::WeatherValue(9, 1, 0); }
// 積もる高さ (cm, 0 .. 5), and how deep the snow lies now (cm).
inline void SetSnowDepth(float cm) { detail::SetWeatherValue(8, cm, 0); }
inline float SnowLying() { return detail::WeatherValue(8, 1, 0); }
// 風: where it blows to (degrees, seen from above: 0 to the front -Z, 90 to the left -X) and how
// strong (m/s). The clouds drift with it, the rain and the snow slant.
inline void SetWind(float directionDegrees, float strength, float seconds = 0) {
    detail::SetWeatherValue(3, directionDegrees, seconds);
    detail::SetWeatherValue(4, strength, seconds);
}
inline float WindDirection() { return detail::WeatherValue(3, 0, 0); }
inline float WindStrength() { return detail::WeatherValue(4, 0, 0); }
// 雷: at once.
inline void SetLightning(bool on) { detail::SetWeatherValue(6, on ? 1.0f : 0.0f, 0); }
// 濡れ: how wet what the rain reaches is now (0 .. 1; wetter while it rains, dries after).
inline float Wetness() { return detail::WeatherValue(7, 1, 0); }
// A fade is going on (0 .. 1 how far it has come; 1: none).
inline float Fading() {
    const RmWeatherApi* s = detail::ApiWeather();
    return s ? s->fading(Api()->ctx) : 1.0f;
}
}  // namespace Sky

}  // namespace rm
