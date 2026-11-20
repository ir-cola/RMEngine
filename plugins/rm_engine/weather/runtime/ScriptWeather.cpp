// スクリプト API の天候の部分 (RMWeather.h)。RmApi::ctx はゲーム制作の ScriptWorld: そのシーンは
// Play しているもの (エディターではコピー) で、その 天候 はそのシーンの設定。

#include "rm_engine/weather/runtime/ScriptWeather.h"

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/weather/runtime/Weather.h"
#include "rm_engine/weather/runtime/sdk/RMWeatherAbi.h"

#include <algorithm>
#include <cmath>

namespace rm::weather {

namespace {

using game_production::ScriptWorld;

Weather* WeatherOf(void* ctx) {
    auto* w = static_cast<ScriptWorld*>(ctx);
    return w && w->scene ? w->scene->Setting<Weather>() : nullptr;
}

int SetWeather(void* ctx, int preset, float seconds) {
    Weather* w = WeatherOf(ctx);
    if (!w || preset < 0 || preset >= kWeatherPresets) return 0;
    BeginFade(*w, seconds);
    ApplyPreset(*w, static_cast<WeatherPreset>(preset));
    return 1;
}

int HasWeather(void* ctx) { return WeatherOf(ctx) ? 0 : -1; }

int SetValue(void* ctx, int which, float value, float seconds) {
    Weather* w = WeatherOf(ctx);
    if (!w || !std::isfinite(value)) return 0;
    if (which < 0 || which > 8 || which == 7) return 0;
    if (which <= 4) BeginFade(*w, seconds);  // (雪にする、雷、積もる高さ: すぐに - 雪は自分で入ってくる)
    auto tenths = [](float v, float most) { return std::trunc(std::clamp(v, 0.f, most) * 10.f + 1e-4f) / 10.f; };  // (インスペクターが持つ形で)
    switch (which) {
        case 0: w->cloudAmount = tenths(value, 100.f); break;
        case 1: w->precipitation = tenths(value, 100.f); break;
        case 2: w->fogAmount = tenths(value, 100.f); break;
        case 3: w->windDirection = std::fmod(value, 360.f); break;  // (フェードは短いほうへ回す: NowOf がベクトルを混ぜる)
        case 4: w->windStrength = std::clamp(value, 0.f, 100.f); break;
        case 5: w->snowing = value != 0; break;
        case 6: w->lightning = value != 0; break;
        case 8: w->snowDepthCm = tenths(value, 5.f); break;
        default: break;
    }
    return 1;
}

int GetValue(void* ctx, int which, int now, float* out) {
    const Weather* w = WeatherOf(ctx);
    if (!w || !out) return 0;
    const WeatherNow n = NowOf(*w);
    switch (which) {
        case 0: *out = now ? n.clouds * 100.f : w->cloudAmount; return 1;
        case 1: *out = now ? RainNow(n) * 100.f : w->precipitation; return 1;
        case 2: *out = now ? n.fog * 100.f : w->fogAmount; return 1;
        case 3: {
            if (!now) {
                *out = w->windDirection;
            } else {  // (WindOf の逆: 0 は -Z、+ は左)
                const float strength = std::sqrt(n.windX * n.windX + n.windZ * n.windZ);
                *out = strength > 1e-6f ? std::atan2(-n.windX, -n.windZ) * 57.2957795f : w->windDirection;
            }
            return 1;
        }
        case 4: *out = now ? std::sqrt(n.windX * n.windX + n.windZ * n.windZ) : w->windStrength; return 1;
        case 5: *out = now ? SnowInOf(*w) : (w->snowing ? 1.f : 0.f); return 1;
        case 6: *out = w->lightning ? 1.f : 0.f; return 1;
        case 7: *out = WetnessOf(WetNow(*w)); return 1;
        case 8: *out = now ? SnowCoverNow(*w) * 100.f : w->snowDepthCm; return 1;
        case 9: *out = SnowNow(n) * 100.f; return 1;
        default: return 0;
    }
}

float Fading(void* ctx) {
    const Weather* w = WeatherOf(ctx);
    if (!w || !(w->fadeSeconds > 0)) return 1.f;
    return std::clamp(w->fadeElapsed / w->fadeSeconds, 0.f, 1.f);
}

constexpr RmWeatherApi kTable{&SetWeather, &HasWeather, &SetValue, &GetValue, &Fading};

}  // namespace

const game_production::ScriptApiPart& ScriptWeatherPart() {
    static const game_production::ScriptApiPart part{RM_PART_WEATHER, RM_PART_WEATHER_VERSION, &kTable};
    return part;
}

}  // namespace rm::weather
