// The weather part of the scripts' API (RMWeather.h). RmApi::ctx is game production's ScriptWorld: its
// scene is the one that plays (the copy, in the editor), and its 天候 is that scene's setting.

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

int SetWeather(void* ctx, int kind, float seconds) {
    Weather* w = WeatherOf(ctx);
    if (!w || kind < 0 || kind > 3) return 0;
    BeginFade(*w, seconds);
    w->kind = static_cast<WeatherKind>(kind);
    return 1;
}

int WeatherSet(void* ctx) {
    const Weather* w = WeatherOf(ctx);
    return w ? static_cast<int>(w->kind) : -1;
}

int SetValue(void* ctx, int which, float value, float seconds) {
    Weather* w = WeatherOf(ctx);
    if (!w || !std::isfinite(value)) return 0;
    if (which < 0 || which > 6 || which == 5) return 0;
    if (which <= 4) BeginFade(*w, seconds);  // (雷: at once)
    switch (which) {
        case 0: (w->kind == WeatherKind::Clear ? w->clearClouds : w->cloudiness) = std::clamp(value, 0.f, 1.f); break;
        case 1: w->rain = std::clamp(value, 0.f, 1.f); break;
        case 2: w->snow = std::clamp(value, 0.f, 1.f); break;
        case 3: w->windDirection = std::fmod(value, 360.f); break;  // (a fade turns it the short way: NowOf blends the vector)
        case 4: w->windStrength = std::clamp(value, 0.f, 100.f); break;
        case 6: w->lightning = value != 0; break;
        default: break;
    }
    return 1;
}

int GetValue(void* ctx, int which, int now, float* out) {
    const Weather* w = WeatherOf(ctx);
    if (!w || !out) return 0;
    const WeatherNow n = now ? NowOf(*w) : TargetOf(*w);
    switch (which) {
        case 0: {
            if (!now) {
                *out = w->kind == WeatherKind::Clear ? w->clearClouds : w->cloudiness;
            } else {  // (a mix: each weather's own amount by its share)
                const float clear = n.weight[static_cast<int>(WeatherKind::Clear)];
                *out = n.clearClouds * clear + n.cloudiness * (1 - clear);
            }
            return 1;
        }
        case 1: *out = now ? RainNow(n) : w->rain; return 1;
        case 2: *out = now ? SnowNow(n) : w->snow; return 1;
        case 3: {
            if (!now) {
                *out = w->windDirection;
            } else {  // (the inverse of WindOf: 0 to -Z, + to the left)
                const float strength = std::sqrt(n.windX * n.windX + n.windZ * n.windZ);
                *out = strength > 1e-6f ? std::atan2(-n.windX, -n.windZ) * 57.2957795f : w->windDirection;
            }
            return 1;
        }
        case 4: *out = now ? std::sqrt(n.windX * n.windX + n.windZ * n.windZ) : w->windStrength; return 1;
        case 6: *out = w->lightning ? 1.f : 0.f; return 1;
        case 7: *out = WetnessOf(WetNow(*w)); return 1;
        default: return 0;
    }
}

float Fading(void* ctx) {
    const Weather* w = WeatherOf(ctx);
    if (!w || !(w->fadeSeconds > 0)) return 1.f;
    return std::clamp(w->fadeElapsed / w->fadeSeconds, 0.f, 1.f);
}

constexpr RmWeatherApi kTable{&SetWeather, &WeatherSet, &SetValue, &GetValue, &Fading};

}  // namespace

const game_production::ScriptApiPart& ScriptWeatherPart() {
    static const game_production::ScriptApiPart part{RM_PART_WEATHER, RM_PART_WEATHER_VERSION, &kTable};
    return part;
}

}  // namespace rm::weather
