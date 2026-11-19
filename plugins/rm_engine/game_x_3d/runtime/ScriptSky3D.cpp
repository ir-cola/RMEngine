// The sky part of the scripts' API (RM3D.h's rm::Sky). RmApi::ctx is game production's ScriptWorld:
// its scene is the one that plays (the copy, in the editor), and its 空 is that scene's setting.

#include "rm_engine/game_x_3d/runtime/ScriptSky3D.h"

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/game_x_3d/runtime/sdk/RM3DAbi.h"

#include <algorithm>
#include <cmath>

namespace rm::game_x_3d {

namespace {

using game_production::ScriptWorld;

Sky* SkyOf(void* ctx) {
    auto* w = static_cast<ScriptWorld*>(ctx);
    return w && w->scene ? w->scene->Setting<Sky>() : nullptr;
}

int SetWeather(void* ctx, int weather, float seconds) {
    Sky* sky = SkyOf(ctx);
    if (!sky || weather < 0 || weather > 3) return 0;
    BeginFade(*sky, seconds);
    sky->weather = static_cast<Weather>(weather);
    return 1;
}

int WeatherSet(void* ctx) {
    const Sky* sky = SkyOf(ctx);
    return sky ? static_cast<int>(sky->weather) : -1;
}

int SetValue(void* ctx, int which, float value, float seconds) {
    Sky* sky = SkyOf(ctx);
    if (!sky || !std::isfinite(value)) return 0;
    switch (which) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4: BeginFade(*sky, seconds); break;
        case 5:
        case 6: break;  // (at once)
        default: return 0;
    }
    switch (which) {
        case 0: (sky->weather == Weather::Clear ? sky->clearClouds : sky->cloudiness) = std::clamp(value, 0.f, 1.f); break;
        case 1: sky->rain = std::clamp(value, 0.f, 1.f); break;
        case 2: sky->snow = std::clamp(value, 0.f, 1.f); break;
        case 3: {
            // The way a fade turns the wind is the short way round (NowOf blends it as a vector).
            sky->windDirection = std::fmod(value, 360.f);
            break;
        }
        case 4: sky->windStrength = std::clamp(value, 0.f, 100.f); break;
        case 5: sky->hour = std::fmod(std::fmod(value, 24.f) + 24.f, 24.f); break;
        case 6: sky->lightning = value != 0; break;
        default: break;
    }
    return 1;
}

int GetValue(void* ctx, int which, int now, float* out) {
    const Sky* sky = SkyOf(ctx);
    if (!sky || !out) return 0;
    const WeatherNow n = now ? NowOf(*sky) : TargetOf(*sky);
    switch (which) {
        case 0: {
            if (!now) {
                *out = sky->weather == Weather::Clear ? sky->clearClouds : sky->cloudiness;
            } else {  // (a mix: each weather's own amount by its share)
                const float clear = n.weight[static_cast<int>(Weather::Clear)];
                *out = n.clearClouds * clear + n.cloudiness * (1 - clear);
            }
            return 1;
        }
        case 1: *out = now ? RainNow(n) : sky->rain; return 1;
        case 2: *out = now ? SnowNow(n) : sky->snow; return 1;
        case 3: {
            if (!now) {
                *out = sky->windDirection;
            } else {  // (the inverse of WindOf: 0 to -Z, + to the left)
                const float strength = std::sqrt(n.windX * n.windX + n.windZ * n.windZ);
                *out = strength > 1e-6f ? std::atan2(-n.windX, -n.windZ) * 57.2957795f : sky->windDirection;
            }
            return 1;
        }
        case 4: *out = now ? std::sqrt(n.windX * n.windX + n.windZ * n.windZ) : sky->windStrength; return 1;
        case 5: *out = sky->hour; return 1;
        case 6: *out = sky->lightning ? 1.f : 0.f; return 1;
        case 7: *out = sky->wetness >= 0 ? sky->wetness : NextWetness(-1, NowOf(*sky), 0); return 1;
        default: return 0;
    }
}

float Fading(void* ctx) {
    const Sky* sky = SkyOf(ctx);
    if (!sky || !(sky->fadeSeconds > 0)) return 1.f;
    return std::clamp(sky->fadeElapsed / sky->fadeSeconds, 0.f, 1.f);
}

constexpr RmSky3DApi kTable{&SetWeather, &WeatherSet, &SetValue, &GetValue, &Fading};

}  // namespace

const game_production::ScriptApiPart& ScriptSky3DPart() {
    static const game_production::ScriptApiPart part{RM_PART_SKY3D, RM_PART_SKY3D_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_3d
