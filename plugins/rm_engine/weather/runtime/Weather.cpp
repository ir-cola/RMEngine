#include "rm_engine/weather/runtime/Weather.h"

#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/weather/runtime/Falling.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::weather {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.f;

float Smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float Unit(float percent) { return std::isfinite(percent) ? std::clamp(percent / 100.f, 0.f, 1.f) : 0.f; }

// 0 .. 1 from a number, the same every time (lightning's when and how bright).
float Hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFFu) / 16777216.f;
}

Vec3 TurnY(Vec3 d, float degrees) {
    const float c = std::cos(degrees * kDegrees), s = std::sin(degrees * kDegrees);
    return {c * d.x + s * d.z, d.y, -s * d.x + c * d.z};
}

}  // namespace

void ApplyPreset(Weather& w, WeatherPreset preset) {
    struct Look {
        float clouds, precipitation;
        bool snowing;
        float fog;
        bool lightning;
        float wind;
    };
    // (雲の量, 降水量, 雪にする, 霧の量, 雷, 風の強さ)
    static constexpr Look kLooks[kWeatherPresets] = {
        {0, 0, false, 0, false, 1},       // 快晴
        {20, 0, false, 0, false, 2},      // 晴れ
        {75, 0, false, 0, false, 3},      // くもり
        {60, 0, false, 60, false, 0.5f},  // 霧
        {65, 6, false, 0, false, 2},      // ぽつぽつ雨
        {80, 22, false, 5, false, 2},     // 小雨
        {90, 50, false, 10, false, 4},    // 雨
        {100, 80, false, 25, false, 7},   // どしゃぶり
        {100, 100, false, 40, true, 22},  // 嵐
        {70, 20, true, 5, false, 1},      // 小雪
        {85, 50, true, 10, false, 2},     // 雪
        {100, 80, true, 25, false, 4},    // 大雪
        {100, 100, true, 50, false, 18},  // 吹雪
    };
    const int k = static_cast<int>(preset);
    if (k < 0 || k >= kWeatherPresets) return;
    const Look& l = kLooks[k];
    w.cloudAmount = l.clouds;
    w.precipitation = l.precipitation;
    w.snowing = l.snowing;
    w.fogAmount = l.fog;
    w.lightning = l.lightning;
    w.windStrength = l.wind;
}

float SnowInOf(const Weather& w) {
    if (!w.wetCounted) return w.snowing ? 1.f : 0.f;
    return std::isfinite(w.snowIn) ? std::clamp(w.snowIn, 0.f, 1.f) : 0.f;
}

float RainShare(float x) { return x < 0.5f ? 1 - 0.85f * Smooth(0.f, 0.5f, x) : 0.f; }
float SnowShare(float x) { return x >= 0.5f ? 0.15f + 0.85f * Smooth(0.5f, 1.f, x) : 0.f; }

float SnowMostOf(const Weather& w) { return std::isfinite(w.snowDepthCm) ? std::clamp(w.snowDepthCm, 0.f, 5.f) / 100.f : 0.f; }

WeatherNow TargetOf(const Weather& w) {
    WeatherNow n;
    const float p = Unit(w.precipitation), x = SnowInOf(w);
    n.clouds = Unit(w.cloudAmount);
    n.rain = p * RainShare(x);
    n.snow = p * SnowShare(x);
    n.fog = Unit(w.fogAmount);
    const Vec3 wind = WindOf(w.windDirection, w.windStrength);
    n.windX = wind.x;
    n.windZ = wind.z;
    return n;
}

WeatherNow NowOf(const Weather& w) {
    WeatherNow to = TargetOf(w);
    if (!(w.fadeSeconds > 0) || !(w.fadeElapsed < w.fadeSeconds)) return to;
    const float t = Smooth(0.f, 1.f, std::max(w.fadeElapsed, 0.f) / w.fadeSeconds);  // eased in and out
    const WeatherNow& a = w.fadeFrom;
    to.clouds = Lerp(a.clouds, to.clouds, t);
    to.rain = Lerp(a.rain, to.rain, t);
    to.snow = Lerp(a.snow, to.snow, t);
    to.fog = Lerp(a.fog, to.fog, t);
    to.windX = Lerp(a.windX, to.windX, t);  // (the wind as a vector: it turns the short way)
    to.windZ = Lerp(a.windZ, to.windZ, t);
    return to;
}

void BeginFade(Weather& w, float seconds) {
    w.fadeFrom = NowOf(w);
    w.fadeSeconds = std::isfinite(seconds) && seconds > 0 ? seconds : 0.f;
    w.fadeElapsed = 0;
}

float RainNow(const WeatherNow& w) { return std::isfinite(w.rain) ? std::clamp(w.rain, 0.f, 1.f) : 0.f; }
float SnowNow(const WeatherNow& w) { return std::isfinite(w.snow) ? std::clamp(w.snow, 0.f, 1.f) : 0.f; }

float FogOf(const WeatherNow& w) {
    // 霧の量: 0.5 a metre at 100 (5 m leaves a twelfth), 0.125 at 50 (20 m leaves a twelfth). A downpour
    // from about 55 and a snowstorm add to it.
    const float fog = std::isfinite(w.fog) ? std::clamp(w.fog, 0.f, 1.f) : 0.f;
    const float rain = Smooth(0.55f, 1.f, RainNow(w)), snow = Smooth(0.5f, 1.f, SnowNow(w));
    return 0.5f * fog * fog + 0.2f * rain * std::sqrt(rain) + 0.15f * snow * std::sqrt(snow);
}

Color FogColorOf(const WeatherNow& w, Color light) {
    // Pale in fair or cloudy weather; darker and greyer (a little blue) in rain; white in snow.
    const float rain = Smooth(0.f, 0.3f, RainNow(w)), snow = Smooth(0.f, 0.3f, SnowNow(w));
    const float k = 0.55f - 0.22f * rain + 0.2f * snow;
    return {light.r * k, light.g * k, light.b * (k + 0.04f * rain), 1};
}

Vec3 WindOf(float directionDegrees, float strength) {
    if (!std::isfinite(directionDegrees) || !(strength > 0) || !std::isfinite(strength)) return {0, 0, 0};
    const Vec3 d = TurnY(Vec3{0, 0, -1}, directionDegrees);  // 0: to the front (-Z), + to the left
    return {d.x * strength, 0, d.z * strength};
}

game_x_3d::SkyWeather SkyWeatherOf(const Weather& w, double seconds) {
    const WeatherNow now = NowOf(w);
    const float c = std::clamp(now.clouds, 0.f, 1.f), r = RainNow(now), sn = SnowNow(now);
    // Rain or snow brings clouds, however few were asked for; rain darkens them into rain clouds, snow's
    // stay paler and hazier. Many clouds dim the sun.
    const float falling = Smooth(0.f, 0.15f, std::max(r, sn));
    const float cover = std::max(c, falling * (0.75f + 0.25f * std::max(r, sn)));
    game_x_3d::SkyWeather s;
    s.cover = cover;
    s.dark = std::clamp((0.15f * cover + 0.45f * Smooth(0.45f, 1.f, cover) + 0.5f * r * (0.5f + 0.5f * cover)) * (1 - 0.35f * sn), 0.f, 1.f);
    s.haze = std::clamp(0.02f + 0.15f * cover * cover + 0.45f * r * r + 0.5f * sn + 0.6f * std::clamp(now.fog, 0.f, 1.f), 0.f, 1.f);
    s.through = std::clamp(1 - 0.75f * Smooth(0.3f, 1.f, cover) - 0.35f * r - 0.15f * sn, 0.06f, 1.f);
    // 雷: now and then (an eighth of a second at a time) a flash - only while the time goes on.
    if (w.lightning && cover > 0.5f && seconds > 0) {
        const auto tick = static_cast<uint32_t>(seconds * 8);
        if (Hash01(tick * 2u) > 0.975f) s.flash = 0.5f + 0.5f * Hash01(tick * 2u + 1u);
    }
    s.driftX = w.driftX;
    s.driftZ = w.driftZ;
    s.windX = now.windX;
    s.windZ = now.windZ;
    return s;
}

namespace {

// What falls now that wets (rain: all of it in the end; snow, where it melts: a little), each with the
// way it falls, how wet it makes things at most and how fast (a second), and how far it seeps under what
// covers at most and how fast (metres a second).
struct Wetting {
    Vec3 way;
    float most, speed;
    float seepMost, seepSpeed;
};
std::vector<Wetting> WettingNow(const WeatherNow& now) {
    std::vector<Wetting> out;
    const Vec3 wind{now.windX, 0, now.windZ};
    // Rain: all of it wet in the end - sooner the harder it rains (a downpour: about 25 s; a drizzle:
    // about two minutes).
    // It seeps under what covers: a downpour 1 m in under two minutes, a drizzle in about seven (2026-11-20).
    if (const float rain = RainNow(now); rain > 0) {
        out.push_back({FallDirection(Precipitation::Kind::Rain, wind), 1.f, 0.004f + rain * 0.036f, kSeepRain, 0.002f + rain * 0.008f});
    }
    // Snow: where it melts, a little wet - slower (2026-11-19), and it seeps less far.
    if (const float snow = SnowNow(now); snow > 0) {
        out.push_back({FallDirection(Precipitation::Kind::Snow, wind), kSnowWet, 0.002f + snow * 0.012f, kSeepSnow, 0.001f + snow * 0.003f});
    }
    return out;
}

}  // namespace

std::vector<WetWay> WetAtOnce(const WeatherNow& now, float snowMost) {
    // (as after a while of it: each falling thing's way as wet as it makes things)
    std::vector<WetWay> out;
    const float close = std::cos(kWetWayDegrees * kDegrees);
    for (const Wetting& w : WettingNow(now)) {
        auto same = std::find_if(out.begin(), out.end(), [&](const WetWay& o) { return o.way.x * w.way.x + o.way.y * w.way.y + o.way.z * w.way.z >= close; });
        if (same != out.end()) {
            same->sun = same->shade = std::max(same->sun, w.most);
            same->seep = std::max(same->seep, w.seepMost);
        } else {
            out.push_back({w.way, w.most, w.most, w.seepMost});
        }
    }
    // The snow lying (as after a while of it): all of 積もる高さ, the way it falls.
    if (SnowNow(now) > 0) {
        const Vec3 snowWay = FallDirection(Precipitation::Kind::Snow, {now.windX, 0, now.windZ});
        auto it = std::find_if(out.begin(), out.end(), [&](const WetWay& o) { return o.way.x * snowWay.x + o.way.y * snowWay.y + o.way.z * snowWay.z >= close; });
        if (it != out.end()) it->snow = StepSnowCover(0, now, snowMost, 1e9f);
    }
    return out;
}

void StepWet(std::vector<WetWay>& ways, const WeatherNow& now, float dt, float snowMost) {
    dt = std::isfinite(dt) ? std::max(dt, 0.f) : 0.f;
    const float close = std::cos(kWetWayDegrees * kDegrees);
    auto dot = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    // 積雪, all of it now and after: shared out below.
    float snowBefore = 0;
    for (const WetWay& w : ways) snowBefore += std::isfinite(w.snow) ? std::max(w.snow, 0.f) : 0.f;
    const float snowAfter = StepSnowCover(snowBefore, now, snowMost, dt);
    auto shareSnow = [&] {
        if (snowAfter > snowBefore && SnowNow(now) > 0) {
            // What fell goes to the way it falls (WettingNow made it a way).
            const Vec3 snowWay = FallDirection(Precipitation::Kind::Snow, {now.windX, 0, now.windZ});
            auto it = std::find_if(ways.begin(), ways.end(), [&](const WetWay& o) { return dot(o.way, snowWay) >= close; });
            if (it != ways.end()) it->snow = std::max(it->snow, 0.f) + (snowAfter - snowBefore);
        } else if (snowAfter < snowBefore) {
            // What melts goes from all of them alike.
            const float keep = snowBefore > 0 ? snowAfter / snowBefore : 0.f;
            for (WetWay& w : ways) w.snow = std::isfinite(w.snow) ? std::max(w.snow, 0.f) * keep : 0.f;
        }
    };
    const std::vector<Wetting> wetting = WettingNow(now);
    if (!wetting.empty()) {
        for (const Wetting& w : wetting) {
            auto it = std::find_if(ways.begin(), ways.end(), [&](const WetWay& o) { return dot(o.way, w.way) >= close; });
            if (it == ways.end()) {
                if (ways.size() < kWetWays) {
                    ways.push_back({w.way, 0, 0});
                    it = ways.end() - 1;
                } else {
                    it = std::min_element(ways.begin(), ways.end(), [](const WetWay& a, const WetWay& b) { return a.sun + a.shade + a.snow < b.sun + b.shade + b.snow; });
                    *it = {w.way, 0, 0};
                }
            }
            // Wetter, up to what it makes (never drier while anything falls).
            for (float* level : {&it->sun, &it->shade}) {
                if (*level < w.most) *level = std::min(w.most, *level + dt * w.speed);
            }
            if (it->seep < w.seepMost) it->seep = std::min(w.seepMost, it->seep + dt * w.seepSpeed);
        }
        shareSnow();
        return;
    }
    for (WetWay& w : ways) {
        w.sun = std::max(0.f, w.sun - dt / 90.f);
        w.shade = std::max(0.f, w.shade - dt / 300.f);
    }
    shareSnow();
    std::erase_if(ways, [](const WetWay& w) { return w.sun <= 0 && w.shade <= 0 && !(w.snow > 0); });
}

float WetnessOf(const std::vector<WetWay>& ways) {
    float most = 0;
    for (const WetWay& w : ways) most = std::max(most, w.sun);
    return most;
}

std::vector<WetWay> WetNow(const Weather& w) { return w.wetCounted ? w.wetWays : WetAtOnce(NowOf(w), SnowMostOf(w)); }

float StepSnowCover(float cover, const WeatherNow& now, float most, float dt) {
    dt = std::isfinite(dt) ? std::max(dt, 0.f) : 0.f;
    most = std::isfinite(most) ? std::clamp(most, 0.f, kMostSnowDepth) : 0.f;
    cover = std::isfinite(cover) ? std::clamp(cover, 0.f, most) : 0.f;
    if (const float rate = SnowFallRate(now, most); rate > 0) {
        return std::min(most, cover + dt * rate);  // (all of 積もる高さ in 240 s at 1, 400 s at 0.5)
    }
    return std::max(0.f, cover - dt * (0.0002f + RainNow(now) * 0.002f));
}

float SnowFallRate(const WeatherNow& now, float most) {
    const float snow = SnowNow(now);
    most = std::isfinite(most) ? std::clamp(most, 0.f, kMostSnowDepth) : 0.f;
    return snow > 0 ? most * (0.1f + snow * 0.9f) / 240.f : 0.f;
}

float SnowCoverNow(const Weather& w) {
    float all = 0;
    for (const WetWay& way : WetNow(w)) all += std::isfinite(way.snow) ? std::max(way.snow, 0.f) : 0.f;
    return all;
}

void RunWeather(game_production::SystemContext& ctx) {
    Weather* w = ctx.scene.Setting<Weather>();
    if (!w) return;
    const float dt = std::isfinite(ctx.dt) ? std::max(ctx.dt, 0.f) : 0.f;
    if (w->fadeSeconds > 0 && w->fadeElapsed < w->fadeSeconds) w->fadeElapsed = std::min(w->fadeElapsed + dt, w->fadeSeconds);
    if (!w->wetCounted) w->snowIn = w->snowing ? 1.f : 0.f;  // (as the editor showed it: then it changes slowly)
    // 雪にする: rain into snow (or back) over about 5 seconds.
    const float snowTo = w->snowing ? 1.f : 0.f, snowFrom = std::isfinite(w->snowIn) ? std::clamp(w->snowIn, 0.f, 1.f) : snowTo;
    w->snowIn = snowFrom < snowTo ? std::min(snowTo, snowFrom + dt / 5.f) : std::max(snowTo, snowFrom - dt / 5.f);
    const WeatherNow now = NowOf(*w);
    if (!w->wetCounted) {
        w->wetWays.clear();  // (the game starts dry and with no snow: it lies as it snows - 2026-11-19 / 20)
        w->wetCounted = true;
    }
    StepWet(w->wetWays, now, dt, SnowMostOf(*w));
    w->driftX = std::fmod(w->driftX + now.windX * dt, kDriftWrap);
    w->driftZ = std::fmod(w->driftZ + now.windZ * dt, kDriftWrap);
}

}  // namespace rm::weather
