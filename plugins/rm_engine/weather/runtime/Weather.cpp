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

// What one weather does to the sky: the clouds' cover and darkness, haze, how much sunlight gets through.
struct CloudLook {
    float cover, dark, haze, through;
};
CloudLook OneWeather(WeatherKind kind, const WeatherNow& n) {
    const float c = std::clamp(n.cloudiness, 0.f, 1.f);
    switch (kind) {
        case WeatherKind::Clear: {
            // A fair sky: the clouds as asked, the sun hardly dimmed (0.2: the few clouds of before).
            const float f = std::clamp(n.clearClouds, 0.f, 1.f);
            return {0.75f * f, 0.12f * f * f, 0.03f * f, 1.f - 0.2f * f * f};
        }
        // くもり / 雨: more cloud and less sunlight than a fair sky ever has.
        case WeatherKind::Cloudy: return {0.4f + 0.6f * c, 0.2f + 0.45f * c, 0.08f + 0.12f * c, 0.7f - 0.5f * c};
        case WeatherKind::Rain: {
            const float r = std::clamp(n.rain, 0.f, 1.f);
            return {0.65f + 0.35f * c, 0.42f + 0.45f * c, 0.12f + 0.25f * r, 0.42f - 0.3f * c};
        }
        case WeatherKind::Snow: {
            const float s = std::clamp(n.snow, 0.f, 1.f);
            return {0.4f + 0.6f * c, 0.1f + 0.2f * c, 0.15f + 0.4f * s, 0.75f - 0.3f * c};
        }
    }
    return {0.15f, 0.f, 0.f, 1.f};  // (a value of a newer version: as a fair sky)
}

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

WeatherNow TargetOf(const Weather& w) {
    WeatherNow n;
    const int k = static_cast<int>(w.kind);
    n.weight[0] = 0;
    n.weight[k >= 0 && k < 4 ? k : 0] = 1;  // (a value of a newer version: as a fair sky)
    n.clearClouds = w.clearClouds;
    n.cloudiness = w.cloudiness;
    n.rain = w.rain;
    n.snow = w.snow;
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
    for (int k = 0; k < 4; ++k) to.weight[k] = Lerp(a.weight[k], to.weight[k], t);
    to.clearClouds = Lerp(a.clearClouds, to.clearClouds, t);
    to.cloudiness = Lerp(a.cloudiness, to.cloudiness, t);
    to.rain = Lerp(a.rain, to.rain, t);
    to.snow = Lerp(a.snow, to.snow, t);
    to.windX = Lerp(a.windX, to.windX, t);  // (the wind as a vector: it turns the short way)
    to.windZ = Lerp(a.windZ, to.windZ, t);
    return to;
}

void BeginFade(Weather& w, float seconds) {
    w.fadeFrom = NowOf(w);
    w.fadeSeconds = std::isfinite(seconds) && seconds > 0 ? seconds : 0.f;
    w.fadeElapsed = 0;
}

float RainNow(const WeatherNow& w) { return std::clamp(w.weight[static_cast<int>(WeatherKind::Rain)] * std::clamp(w.rain, 0.f, 1.f), 0.f, 1.f); }
float SnowNow(const WeatherNow& w) { return std::clamp(w.weight[static_cast<int>(WeatherKind::Snow)] * std::clamp(w.snow, 0.f, 1.f), 0.f, 1.f); }

Vec3 WindOf(float directionDegrees, float strength) {
    if (!std::isfinite(directionDegrees) || !(strength > 0) || !std::isfinite(strength)) return {0, 0, 0};
    const Vec3 d = TurnY(Vec3{0, 0, -1}, directionDegrees);  // 0: to the front (-Z), + to the left
    return {d.x * strength, 0, d.z * strength};
}

game_x_3d::SkyWeather SkyWeatherOf(const Weather& w, double seconds) {
    const WeatherNow now = NowOf(w);
    // Each of the four's look by its share (one alone, or a mix while it fades).
    CloudLook sum{0, 0, 0, 0};
    for (int k = 0; k < 4; ++k) {
        const float wk = now.weight[k];
        if (!(wk > 0)) continue;
        const CloudLook one = OneWeather(static_cast<WeatherKind>(k), now);
        sum.cover += one.cover * wk;
        sum.dark += one.dark * wk;
        sum.haze += one.haze * wk;
        sum.through += one.through * wk;
    }
    game_x_3d::SkyWeather s;
    s.cover = sum.cover;
    s.dark = sum.dark;
    s.haze = sum.haze;
    s.through = sum.through;
    // 雷: now and then (an eighth of a second at a time) a flash - only while the time goes on.
    const float stormy = now.weight[static_cast<int>(WeatherKind::Cloudy)] + now.weight[static_cast<int>(WeatherKind::Rain)];
    if (w.lightning && stormy > 0.5f && seconds > 0) {
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

std::vector<WetWay> WetAtOnce(const WeatherNow& now) {
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
    return out;
}

void StepWet(std::vector<WetWay>& ways, const WeatherNow& now, float dt) {
    dt = std::isfinite(dt) ? std::max(dt, 0.f) : 0.f;
    const std::vector<Wetting> wetting = WettingNow(now);
    if (!wetting.empty()) {
        const float close = std::cos(kWetWayDegrees * kDegrees);
        auto dot = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        for (const Wetting& w : wetting) {
            auto it = std::find_if(ways.begin(), ways.end(), [&](const WetWay& o) { return dot(o.way, w.way) >= close; });
            if (it == ways.end()) {
                if (ways.size() < kWetWays) {
                    ways.push_back({w.way, 0, 0});
                    it = ways.end() - 1;
                } else {
                    it = std::min_element(ways.begin(), ways.end(), [](const WetWay& a, const WetWay& b) { return a.sun + a.shade < b.sun + b.shade; });
                    *it = {w.way, 0, 0};
                }
            }
            // Wetter, up to what it makes (never drier while anything falls).
            for (float* level : {&it->sun, &it->shade}) {
                if (*level < w.most) *level = std::min(w.most, *level + dt * w.speed);
            }
            if (it->seep < w.seepMost) it->seep = std::min(w.seepMost, it->seep + dt * w.seepSpeed);
        }
        return;
    }
    for (WetWay& w : ways) {
        w.sun = std::max(0.f, w.sun - dt / 90.f);
        w.shade = std::max(0.f, w.shade - dt / 300.f);
    }
    std::erase_if(ways, [](const WetWay& w) { return w.sun <= 0 && w.shade <= 0; });
}

float WetnessOf(const std::vector<WetWay>& ways) {
    float most = 0;
    for (const WetWay& w : ways) most = std::max(most, w.sun);
    return most;
}

std::vector<WetWay> WetNow(const Weather& w) { return w.wetCounted ? w.wetWays : WetAtOnce(NowOf(w)); }

void RunWeather(game_production::SystemContext& ctx) {
    Weather* w = ctx.scene.Setting<Weather>();
    if (!w) return;
    const float dt = std::isfinite(ctx.dt) ? std::max(ctx.dt, 0.f) : 0.f;
    if (w->fadeSeconds > 0 && w->fadeElapsed < w->fadeSeconds) w->fadeElapsed = std::min(w->fadeElapsed + dt, w->fadeSeconds);
    const WeatherNow now = NowOf(*w);
    if (!w->wetCounted) {
        w->wetWays.clear();  // (the game starts dry: the rain wets it as it plays - 2026-11-19)
        w->wetCounted = true;
    }
    StepWet(w->wetWays, now, dt);
    w->driftX = std::fmod(w->driftX + now.windX * dt, kDriftWrap);
    w->driftZ = std::fmod(w->driftZ + now.windZ * dt, kDriftWrap);
}

}  // namespace rm::weather
