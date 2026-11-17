#include "rm_engine/game_x_3d/runtime/Sky.h"

#include "rm_engine/game_production/runtime/Systems.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace rm::game_x_3d {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.f;

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
WeatherLook WeatherOf(const Sky& sky) {
    const float c = std::clamp(sky.cloudiness, 0.f, 1.f);
    switch (sky.weather) {
        case Weather::Clear: return {0.15f, 0.f, 0.f, 1.f};
        case Weather::Cloudy: return {0.15f + 0.85f * c, 0.35f * c, 0.12f * c, 1 - 0.6f * c};
        case Weather::Rain: {
            const float r = std::clamp(sky.rain, 0.f, 1.f);
            return {0.4f + 0.6f * c, 0.25f + 0.45f * c, 0.1f + 0.25f * r, 0.65f - 0.4f * c};
        }
        case Weather::Snow: {
            const float s = std::clamp(sky.snow, 0.f, 1.f);
            return {0.4f + 0.6f * c, 0.1f + 0.2f * c, 0.15f + 0.4f * s, 0.75f - 0.3f * c};
        }
    }
    return {0.15f, 0.f, 0.f, 1.f};
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
            const WeatherLook w = WeatherOf(sky);
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
            if (sky.lightning && (sky.weather == Weather::Cloudy || sky.weather == Weather::Rain) && seconds > 0) {
                const auto tick = static_cast<uint32_t>(seconds * 8);
                if (Hash01(tick * 2u) > 0.975f) look.flash = 0.5f + 0.5f * Hash01(tick * 2u + 1u);
            }
            Set(k.param[1], {w.cover, w.dark, w.haze}, look.flash);
            Set(k.param[2], zenith, night * night * (1 - w.cover));
            Set(k.param[3], horizon, night * moonUp);
            Set(k.param[4], ground, glow * up);
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
            if (sky.weather == Weather::Rain || sky.weather == Weather::Snow) {
                const bool rain = sky.weather == Weather::Rain;
                look.precipitation.kind = rain ? space3d::Precipitation::Kind::Rain : space3d::Precipitation::Kind::Snow;
                look.precipitation.amount = std::clamp(rain ? sky.rain : sky.snow, 0.f, 1.f);
                look.precipitation.seconds = static_cast<float>(seconds);
                const Vec3 lit = Add(Add(Scale(skyLight, 2.0f), Scale(Rgb(look.lightColor), look.lightStrength * 0.9f)), Vec3{look.flash, look.flash, look.flash});
                look.precipitation.light = ToColor(lit);
            }
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
    if (!sky || sky->kind != SkyKind::Earth || sky->dayLength <= 0) return;
    sky->hour = std::fmod(std::fmod(sky->hour, 24.f) + ctx.dt * 24.f / sky->dayLength + 24.f, 24.f);
}

}  // namespace rm::game_x_3d
