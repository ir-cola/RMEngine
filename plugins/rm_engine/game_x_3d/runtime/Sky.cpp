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
// 上の軸のまわりに回す (度。上から見て左回りが +) - 空のシェーダーの RotateY と同じ。
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

// 太陽の高さ e (水平線からの角度の sin) に対する空気の色 (リニア)。
void EarthAir(float e, const SkyWeather& w, Vec3& zenith, Vec3& horizon, float& day, float& glow) {
    day = Smooth(-0.1f, 0.15f, e);
    glow = e > -0.25f ? std::exp(-(e / 0.18f) * (e / 0.18f)) : 0.f;  // 日の出 / 日の入り
    zenith = Mix(Vec3{0.003f, 0.006f, 0.016f}, Vec3{0.16f, 0.36f, 0.85f}, day);
    zenith = Mix(zenith, Vec3{0.10f, 0.12f, 0.30f}, glow * 0.6f);
    horizon = Mix(Vec3{0.012f, 0.018f, 0.035f}, Vec3{0.55f, 0.70f, 0.92f}, day);
    horizon = Mix(horizon, Vec3{1.0f, 0.48f, 0.20f}, glow * 0.8f);
    // 雲と雨: より灰色に、より暗く (場所ごとの雨雲は雲の層で描く: ここはシーン全体のもの)。
    const float grey = w.cover * 0.8f;
    zenith = Scale(Mix(zenith, Scale(Vec3{1, 1, 1}, Luminance(zenith)), grey), 1 - w.dark * 0.6f);
    horizon = Scale(Mix(horizon, Scale(Vec3{1, 1, 1}, Luminance(horizon)), grey), 1 - w.dark * 0.5f);
}

// 雲の地図を空の絵の形 (マスごとに 雲の量、降る量、雨雲の近さ、0: 0 .. 1。数でないものは 0) にする。
// 雲の量か降る量の数が合わない: 無し (近さの数が合わない: どこも 0)。
void PackCloudMap(const SkyWeather& w, SkyLook& look) {
    const int n = w.cloudCells;
    const size_t count = n > 0 ? static_cast<size_t>(n) * static_cast<size_t>(n) : 0;
    if (count == 0 || w.cloudCovers.size() != count || w.cloudRains.size() != count || !(w.cloudCell > 0) || !std::isfinite(w.cloudCell) ||
        !std::isfinite(w.cloudFirstX) || !std::isfinite(w.cloudFirstZ))
        return;
    auto unit = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f; };
    const bool nears = w.cloudNears.size() == count;
    look.clouds.assign(count * 4, 0.f);
    for (size_t k = 0; k < count; ++k) {
        look.clouds[k * 4] = unit(w.cloudCovers[k]);
        look.clouds[k * 4 + 1] = unit(w.cloudRains[k]);
        look.clouds[k * 4 + 2] = nears ? unit(w.cloudNears[k]) : 0.f;
    }
    look.cloudCells = static_cast<uint32_t>(n);
    float* m = look.constants.param[6];
    m[0] = w.cloudFirstX;
    m[1] = w.cloudFirstZ;
    m[2] = w.cloudCell;
    m[3] = static_cast<float>(n);
}

}  // namespace

SkyWeather SkyWeatherOf(const RuntimeRegistry* registry, const game_production::Scene& scene, double seconds, Vec3 eye) {
    SkyWeather w;
    if (!registry) return w;
    for (const auto& c : registry->Contributions<SkyWeatherInfo>(kSkyWeatherPoint)) {
        if (c.item->of && c.item->of(*registry, scene, seconds, eye, w)) break;
    }
    return w;
}

Vec3 SunDirection(const Sky& sky) {
    const float a = std::clamp(sky.sunHeight, -90.f, 90.f) * kDegrees;
    const Vec3 noon{0, std::sin(a), std::cos(a)};  // +Z 側、sunHeight の分だけ上
    Vec3 d = noon;
    if (sky.kind == SkyKind::Earth) {
        // 東 (+X、6 時)、正午、西 (-X、18 時) を通る円のまわり。
        const float s = (std::fmod(sky.hour, 24.f) - 12.f) / 24.f * 2.f * std::numbers::pi_v<float>;
        d = Add(Scale(noon, std::cos(s)), Scale(Vec3{-1, 0, 0}, std::sin(s)));
    }
    // 左手系 (2026-11-20 から): 前の右手系の空をそのまま Z で写す (シーンも写したので、見た目は同じ)。
    const Vec3 r = Normalized(TurnY(d, sky.sunDirection));
    return {r.x, r.y, -r.z};
}

SkyLook LookOf(const Sky& sky, double seconds, const SkyWeather& weather) {
    SkyLook look;
    space3d::SkyConstants& k = look.constants;
    const Vec3 sun = SunDirection(sky);
    const Vec3 moon = Scale(sun, -1);  // 満月。太陽の反対側
    Set(k.sun, sun, static_cast<float>(seconds));
    Set(k.moon, moon, sky.kind == SkyKind::Earth ? std::fmod(std::max(sky.hour, 0.f), 24.f) : 12.f);
    k.info[0] = std::max(sky.brightness, 0.f);
    switch (sky.kind) {
        case SkyKind::None: break;
        case SkyKind::Earth: {
            look.builtin = space3d::BuiltinSky::Earth;
            const SkyWeather& w = weather;
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
            look.flash = std::clamp(w.flash, 0.f, 1.f);  // 雷 (天候のもの)
            Set(k.param[1], {w.cover, w.dark, w.haze}, look.flash);
            Set(k.param[2], zenith, night * night * (1 - w.cover));
            Set(k.param[3], horizon, night * moonUp);
            Set(k.param[4], ground, glow * up);
            // 風: 雲をどれだけ運んだか (xy)、今どう吹いているか (zw: m/s、ワールド)。
            k.param[5][0] = w.driftX;
            k.param[5][1] = w.driftZ;
            k.param[5][2] = w.windX;
            k.param[5][3] = w.windZ;
            // 雨雲 (シーン全体の降る量と、そのうち雪の割合)。
            k.param[7][0] = std::isfinite(w.rain) ? std::clamp(w.rain, 0.f, 1.f) : 0.f;
            k.param[7][1] = std::isfinite(w.snow) ? std::clamp(w.snow, 0.f, 1.f) : 0.f;
            PackCloudMap(w, look);  // 場所ごとの雲と雨雲 (param[6] と clouds)
            // 光: 太陽が出ている間は太陽、そのあとは月 (弱い青)。
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
            // 空全体からの光 (環境光): その色。夜は少しだけ月の光。
            const Vec3 skyLight = Add(Scale(Add(Scale(zenith, 0.6f), Scale(horizon, 0.4f)), 0.35f), Scale(Vec3{0.02f, 0.025f, 0.045f}, night));
            look.ambientSky = ToColor(skyLight);
            look.ambientGround = ToColor(Add(Scale(ground, 0.6f), Scale(sunLight, 0.04f * w.through)));
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
        lights.insert(lights.begin(), space3d::Light{});  // (平行光を先頭に: 太陽の影を落とすことがある)
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
