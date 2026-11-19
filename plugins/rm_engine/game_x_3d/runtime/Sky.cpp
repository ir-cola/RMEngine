#include "rm_engine/game_x_3d/runtime/Sky.h"

#include "rm_engine/game_production/runtime/Systems.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace rm::game_x_3d {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.f;
// The wind's drift starts again from 0 this far (world units): a multiple of the rain's and the snow's
// boxes (they look the same there), and far enough that the clouds' jump is seldom (a big storm: hours).
constexpr float kDriftWrap = space3d::kRainBox * space3d::kSnowBox * 400.f;

float Smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
Vec3 Mix(Vec3 a, Vec3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }
Vec3 Scale(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Rgb(const Color& c) { return {c.r, c.g, c.b}; }
Color ToColor(Vec3 v) { return {v.x, v.y, v.z, 1}; }
float Luminance(Vec3 c) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; }
Vec3 Normalized(Vec3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return len > 1e-8f ? Scale(v, 1.f / len) : Vec3{0, 1, 0};
}
// Turned about the up axis (degrees; + to the left seen from above) - as RotateY in the sky's shader.
Vec3 TurnY(Vec3 d, float degrees) {
    const float c = std::cos(degrees * kDegrees), s = std::sin(degrees * kDegrees);
    return {c * d.x + s * d.z, d.y, -s * d.x + c * d.z};
}
void Set(float out[4], Vec3 rgb, float a) {
    out[0] = rgb.x;
    out[1] = rgb.y;
    out[2] = rgb.z;
    out[3] = a;
}

// What the weather does: the clouds' cover and darkness, haze, how much sunlight gets through.
struct WeatherLook {
    float cover, dark, haze, through;
};
WeatherLook OneWeather(Weather kind, const WeatherNow& n) {
    const float c = std::clamp(n.cloudiness, 0.f, 1.f);
    switch (kind) {
        case Weather::Clear: {
            // A fair sky: the clouds as asked, the sun hardly dimmed (0.2: the few clouds of before).
            const float f = std::clamp(n.clearClouds, 0.f, 1.f);
            return {0.75f * f, 0.12f * f * f, 0.03f * f, 1.f - 0.2f * f * f};
        }
        // くもり / 雨: more cloud and less sunlight than a fair sky ever has (2026-11-19: darker than before).
        case Weather::Cloudy: return {0.4f + 0.6f * c, 0.2f + 0.45f * c, 0.08f + 0.12f * c, 0.7f - 0.5f * c};
        case Weather::Rain: {
            const float r = std::clamp(n.rain, 0.f, 1.f);
            return {0.65f + 0.35f * c, 0.42f + 0.45f * c, 0.12f + 0.25f * r, 0.42f - 0.3f * c};
        }
        case Weather::Snow: {
            const float s = std::clamp(n.snow, 0.f, 1.f);
            return {0.4f + 0.6f * c, 0.1f + 0.2f * c, 0.15f + 0.4f * s, 0.75f - 0.3f * c};
        }
    }
    return {0.15f, 0.f, 0.f, 1.f};  // (a value of a newer version: as a fair sky)
}
// The weather now: each of the four's look by its share (one alone, or a mix while it fades).
WeatherLook WeatherOf(const WeatherNow& n) {
    WeatherLook sum{0, 0, 0, 0};
    for (int k = 0; k < 4; ++k) {
        const float wk = n.weight[k];
        if (!(wk > 0)) continue;
        const WeatherLook one = OneWeather(static_cast<Weather>(k), n);
        sum.cover += one.cover * wk;
        sum.dark += one.dark * wk;
        sum.haze += one.haze * wk;
        sum.through += one.through * wk;
    }
    return sum;
}
float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// 0 .. 1 from a number, the same every time (lightning's when and how bright).
float Hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFFu) / 16777216.f;
}

// The air's colours (linear) for the sun's height e (the sine of its angle above the horizon).
void EarthAir(float e, const WeatherLook& w, Vec3& zenith, Vec3& horizon, float& day, float& glow) {
    day = Smooth(-0.1f, 0.15f, e);
    glow = e > -0.25f ? std::exp(-(e / 0.18f) * (e / 0.18f)) : 0.f;  // sunrise / sunset
    zenith = Mix(Vec3{0.003f, 0.006f, 0.016f}, Vec3{0.16f, 0.36f, 0.85f}, day);
    zenith = Mix(zenith, Vec3{0.10f, 0.12f, 0.30f}, glow * 0.6f);
    horizon = Mix(Vec3{0.012f, 0.018f, 0.035f}, Vec3{0.55f, 0.70f, 0.92f}, day);
    horizon = Mix(horizon, Vec3{1.0f, 0.48f, 0.20f}, glow * 0.8f);
    // Clouds and rain: greyer and darker.
    const float grey = w.cover * 0.8f;
    zenith = Scale(Mix(zenith, Scale(Vec3{1, 1, 1}, Luminance(zenith)), grey), 1 - w.dark * 0.6f);
    horizon = Scale(Mix(horizon, Scale(Vec3{1, 1, 1}, Luminance(horizon)), grey), 1 - w.dark * 0.5f);
}

}  // namespace

WeatherNow TargetOf(const Sky& sky) {
    WeatherNow n;
    const int k = static_cast<int>(sky.weather);
    n.weight[0] = 0;
    n.weight[k >= 0 && k < 4 ? k : 0] = 1;  // (a value of a newer version: as a fair sky)
    n.clearClouds = sky.clearClouds;
    n.cloudiness = sky.cloudiness;
    n.rain = sky.rain;
    n.snow = sky.snow;
    const Vec3 wind = WindOf(sky.windDirection, sky.windStrength);
    n.windX = wind.x;
    n.windZ = wind.z;
    return n;
}

WeatherNow NowOf(const Sky& sky) {
    WeatherNow to = TargetOf(sky);
    if (!(sky.fadeSeconds > 0) || !(sky.fadeElapsed < sky.fadeSeconds)) return to;
    const float t = Smooth(0.f, 1.f, std::max(sky.fadeElapsed, 0.f) / sky.fadeSeconds);  // eased in and out
    const WeatherNow& a = sky.fadeFrom;
    for (int k = 0; k < 4; ++k) to.weight[k] = Lerp(a.weight[k], to.weight[k], t);
    to.clearClouds = Lerp(a.clearClouds, to.clearClouds, t);
    to.cloudiness = Lerp(a.cloudiness, to.cloudiness, t);
    to.rain = Lerp(a.rain, to.rain, t);
    to.snow = Lerp(a.snow, to.snow, t);
    to.windX = Lerp(a.windX, to.windX, t);  // (the wind as a vector: it turns the short way)
    to.windZ = Lerp(a.windZ, to.windZ, t);
    return to;
}

void BeginFade(Sky& sky, float seconds) {
    sky.fadeFrom = NowOf(sky);
    sky.fadeSeconds = std::isfinite(seconds) && seconds > 0 ? seconds : 0.f;
    sky.fadeElapsed = 0;
}

float RainNow(const WeatherNow& w) { return std::clamp(w.weight[static_cast<int>(Weather::Rain)] * std::clamp(w.rain, 0.f, 1.f), 0.f, 1.f); }
float SnowNow(const WeatherNow& w) { return std::clamp(w.weight[static_cast<int>(Weather::Snow)] * std::clamp(w.snow, 0.f, 1.f), 0.f, 1.f); }

Vec3 WindOf(float directionDegrees, float strength) {
    if (!std::isfinite(directionDegrees) || !(strength > 0) || !std::isfinite(strength)) return {0, 0, 0};
    return Scale(TurnY(Vec3{0, 0, -1}, directionDegrees), strength);  // 0: to the front (-Z), + to the left
}

std::vector<space3d::WetWay> WetAtOnce(const WeatherNow& now) {
    const float rain = RainNow(now);
    if (!(rain > 0)) return {};
    const float target = std::min(rain * 2.f, 1.f);
    return {{space3d::FallDirection(space3d::Precipitation::Kind::Rain, {now.windX, 0, now.windZ}), target, target}};
}

void StepWet(std::vector<space3d::WetWay>& ways, const WeatherNow& now, float dt) {
    dt = std::isfinite(dt) ? std::max(dt, 0.f) : 0.f;
    const float rain = RainNow(now);
    if (rain > 0) {
        const float target = std::min(rain * 2.f, 1.f);
        const Vec3 way = space3d::FallDirection(space3d::Precipitation::Kind::Rain, {now.windX, 0, now.windZ});
        const float close = std::cos(kWetWayDegrees * kDegrees);
        auto dot = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        auto it = std::find_if(ways.begin(), ways.end(), [&](const space3d::WetWay& w) { return dot(w.way, way) >= close; });
        if (it == ways.end()) {
            if (ways.size() < space3d::kWetWays) {
                ways.push_back({way, 0, 0});
                it = ways.end() - 1;
            } else {
                it = std::min_element(ways.begin(), ways.end(), [](const space3d::WetWay& a, const space3d::WetWay& b) { return a.sun + a.shade < b.sun + b.shade; });
                *it = {way, 0, 0};
            }
        }
        // Wetter (never drier while it rains).
        for (float* level : {&it->sun, &it->shade}) {
            if (*level < target) *level = std::min(target, *level + dt / 8.f);
        }
        return;
    }
    for (space3d::WetWay& w : ways) {
        w.sun = std::max(0.f, w.sun - dt / 90.f);
        w.shade = std::max(0.f, w.shade - dt / 300.f);
    }
    std::erase_if(ways, [](const space3d::WetWay& w) { return w.sun <= 0 && w.shade <= 0; });
}

float WetnessOf(const std::vector<space3d::WetWay>& ways) {
    float most = 0;
    for (const space3d::WetWay& w : ways) most = std::max(most, w.sun);
    return most;
}

Vec3 SunDirection(const Sky& sky) {
    const float a = std::clamp(sky.sunHeight, -90.f, 90.f) * kDegrees;
    const Vec3 noon{0, std::sin(a), std::cos(a)};  // the +Z side, sunHeight up
    Vec3 d = noon;
    if (sky.kind == SkyKind::Earth) {
        // Round a circle through the east (+X, at 6), noon and the west (-X, at 18).
        const float s = (std::fmod(sky.hour, 24.f) - 12.f) / 24.f * 2.f * std::numbers::pi_v<float>;
        d = Add(Scale(noon, std::cos(s)), Scale(Vec3{-1, 0, 0}, std::sin(s)));
    }
    return Normalized(TurnY(d, sky.sunDirection));
}

SkyLook LookOf(const Sky& sky, double seconds) {
    SkyLook look;
    space3d::SkyConstants& k = look.constants;
    const Vec3 sun = SunDirection(sky);
    const Vec3 moon = Scale(sun, -1);  // a full moon, opposite the sun
    Set(k.sun, sun, static_cast<float>(seconds));
    Set(k.moon, moon, sky.kind == SkyKind::Earth ? std::fmod(std::max(sky.hour, 0.f), 24.f) : 12.f);
    k.info[0] = std::max(sky.brightness, 0.f);
    switch (sky.kind) {
        case SkyKind::None: break;
        case SkyKind::Earth: {
            look.builtin = space3d::BuiltinSky::Earth;
            const WeatherNow now = NowOf(sky);
            const WeatherLook w = WeatherOf(now);
            Vec3 zenith, horizon;
            float day = 0, glow = 0;
            EarthAir(sun.y, w, zenith, horizon, day, glow);
            const float up = Smooth(-0.04f, 0.06f, sun.y);
            const Vec3 sunColor = Mix(Vec3{1.0f, 0.42f, 0.16f}, Vec3{1.0f, 0.95f, 0.88f}, Smooth(0.f, 0.3f, sun.y));
            const Vec3 sunLight = Scale(sunColor, up);
            const float night = 1 - day;
            const float moonUp = Smooth(-0.02f, 0.1f, moon.y);
            const Vec3 ground = Add(Scale(horizon, 0.25f), Scale(Vec3{0.02f, 0.018f, 0.015f}, day));
            Set(k.param[0], sunLight, day);
            // 雷: now and then (an eighth of a second at a time) a flash - only while the time goes on.
            if (sky.lightning && now.weight[static_cast<int>(Weather::Cloudy)] + now.weight[static_cast<int>(Weather::Rain)] > 0.5f && seconds > 0) {
                const auto tick = static_cast<uint32_t>(seconds * 8);
                if (Hash01(tick * 2u) > 0.975f) look.flash = 0.5f + 0.5f * Hash01(tick * 2u + 1u);
            }
            Set(k.param[1], {w.cover, w.dark, w.haze}, look.flash);
            Set(k.param[2], zenith, night * night * (1 - w.cover));
            Set(k.param[3], horizon, night * moonUp);
            Set(k.param[4], ground, glow * up);
            // 風: how far it has carried the clouds (xy), how it blows now (zw: m/s, world).
            k.param[5][0] = sky.driftX;
            k.param[5][1] = sky.driftZ;
            k.param[5][2] = now.windX;
            k.param[5][3] = now.windZ;
            // The light: the sun while it is up, the moon (faint, blue) after.
            look.drivesLight = sky.driveLight;
            const float sunStrength = up * w.through;
            const float moonStrength = 0.15f * moonUp * night * w.through;
            if (sunStrength >= moonStrength) {
                look.lightGoes = Scale(sun, -1);
                look.lightColor = ToColor(sunColor);
                look.lightStrength = sunStrength;
            } else {
                look.lightGoes = Scale(moon, -1);
                look.lightColor = {0.55f, 0.65f, 1.0f, 1};
                look.lightStrength = moonStrength;
            }
            // The light from the whole sky (環境光): its colours, a little moonlight at night.
            const Vec3 skyLight = Add(Scale(Add(Scale(zenith, 0.6f), Scale(horizon, 0.4f)), 0.35f), Scale(Vec3{0.02f, 0.025f, 0.045f}, night));
            look.ambientSky = ToColor(skyLight);
            look.ambientGround = ToColor(Add(Scale(ground, 0.6f), Scale(sunLight, 0.04f * w.through)));
            // 雨 / 雪: lit by the sky and the sun / moon (and the flash) - about as much as the ground under
            // them (2026-11-18 on screen: with 0.35 of the moonlight, snow at night was darker than the
            // moonlit ground, like dark spots).
            // Both while 雨 fades into 雪 (or back), each as much as its share; blown by the wind.
            const Color lit = ToColor(Add(Add(Scale(skyLight, 2.0f), Scale(Rgb(look.lightColor), look.lightStrength * 0.9f)), Vec3{look.flash, look.flash, look.flash}));
            look.rain.kind = space3d::Precipitation::Kind::Rain;
            look.snow.kind = space3d::Precipitation::Kind::Snow;
            look.rain.amount = RainNow(now);
            look.snow.amount = SnowNow(now);
            for (space3d::Precipitation* p : {&look.rain, &look.snow}) {
                p->seconds = static_cast<float>(seconds);
                p->light = lit;
                p->driftX = sky.driftX;
                p->driftZ = sky.driftZ;
            }
            look.wind = {now.windX, 0, now.windZ};
            // 濡れ: as the game has counted it (Play), or as wet as the rain now makes things.
            look.wetWays = sky.wetCounted ? sky.wetWays : WetAtOnce(now);
            break;
        }
        case SkyKind::Space: {
            look.builtin = space3d::BuiltinSky::Space;
            Set(k.param[0], {std::max(sky.stars, 0.f), std::max(sky.nebula, 0.f), std::max(sky.milkyWay, 0.f)}, sky.showSun ? 1.f : 0.f);
            Set(k.param[1], Rgb(sky.nebulaColor1), 1);
            Set(k.param[2], Rgb(sky.nebulaColor2), 1);
            look.drivesLight = sky.driveLight;
            look.lightGoes = Scale(sun, -1);
            look.lightColor = {1.0f, 0.97f, 0.92f, 1};
            look.lightStrength = 1;
            const Vec3 glowOf = Scale(Add(Rgb(sky.nebulaColor1), Rgb(sky.nebulaColor2)), 0.03f * std::max(sky.nebula, 0.f));
            look.ambientSky = ToColor(Add(glowOf, Vec3{0.02f, 0.02f, 0.025f}));
            look.ambientGround = {0.01f, 0.01f, 0.012f, 1};
            break;
        }
        case SkyKind::Panorama:
            look.builtin = space3d::BuiltinSky::Panorama;
            k.param[0][0] = sky.turn * kDegrees;
            break;
        case SkyKind::SixImages:
            look.builtin = space3d::BuiltinSky::SixFaces;
            k.param[0][0] = sky.turn * kDegrees;
            break;
        case SkyKind::Custom:
            Set(k.param[0], sky.value, 0);
            k.param[1][0] = sky.color.r;
            k.param[1][1] = sky.color.g;
            k.param[1][2] = sky.color.b;
            k.param[1][3] = sky.color.a;
            break;
    }
    return look;
}

void FollowSky(const SkyLook& look, std::vector<space3d::Light>& lights, space3d::Ambient& ambient) {
    if (!look.drivesLight) return;
    auto sun = std::find_if(lights.begin(), lights.end(), [](const space3d::Light& l) { return l.type == space3d::LightType::Directional; });
    if (sun == lights.end()) {
        lights.insert(lights.begin(), space3d::Light{});  // (a directional light first: it may cast the sun's shadows)
        sun = lights.begin();
    }
    sun->direction = look.lightGoes;
    sun->color = look.lightColor;
    sun->intensity *= look.lightStrength;
    const float f = look.flash * 0.8f;
    ambient.sky = {look.ambientSky.r + f, look.ambientSky.g + f, look.ambientSky.b + f * 1.1f, 1};
    ambient.ground = {look.ambientGround.r + f * 0.5f, look.ambientGround.g + f * 0.5f, look.ambientGround.b + f * 0.55f, 1};
}

void RunSkyClock(game_production::SystemContext& ctx) {
    Sky* sky = ctx.scene.Setting<Sky>();
    if (!sky || sky->kind != SkyKind::Earth) return;
    if (sky->dayLength > 0) sky->hour = std::fmod(std::fmod(sky->hour, 24.f) + ctx.dt * 24.f / sky->dayLength + 24.f, 24.f);
    if (sky->fadeSeconds > 0 && sky->fadeElapsed < sky->fadeSeconds) sky->fadeElapsed = std::min(sky->fadeElapsed + std::max(ctx.dt, 0.f), sky->fadeSeconds);
    const WeatherNow now = NowOf(*sky);
    if (!sky->wetCounted) {
        sky->wetWays = WetAtOnce(now);  // (raining from the start: wet from the start)
        sky->wetCounted = true;
    }
    StepWet(sky->wetWays, now, ctx.dt);
    const float dt = std::max(ctx.dt, 0.f);
    sky->driftX = std::fmod(sky->driftX + now.windX * dt, kDriftWrap);
    sky->driftZ = std::fmod(sky->driftZ + now.windZ * dt, kDriftWrap);
}

}  // namespace rm::game_x_3d
