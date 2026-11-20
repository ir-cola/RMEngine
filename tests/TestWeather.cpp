// Tests for the plugin 天候 (weather/runtime): what each weather tells the sky, lightning, a fade, the
// wind, 濡れ, what falls, and that its shaders compile (D3DCompile, as it is drawn) - with 3D 空間's.

#include "Test.h"

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "core/reflect/Data.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/RendererShaders.h"
#include "rm_engine/space3d/runtime/SkyShader.h"
#include "rm_engine/space3d/runtime/View.h"
#include "rm_engine/weather/runtime/Falling.h"
#include "rm_engine/weather/runtime/Weather.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

using namespace rm;
using namespace rm::weather;
using game_x_3d::LookOf;
using game_x_3d::Sky;
using game_x_3d::SkyKind;
using game_x_3d::SkyLook;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool Near(Vec3 a, Vec3 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

bool Compiles(const std::string& hlsl, const char* entry, const char* target, std::string* error = nullptr) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(hlsl.data(), hlsl.size(), "weather", nullptr, nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    if (FAILED(hr) && error && errors) *error = static_cast<const char*>(errors->GetBufferPointer());
    return SUCCEEDED(hr);
}

Weather Of(WeatherPreset preset) {
    Weather w;
    ApplyPreset(w, preset);
    return w;
}

// 地球の空 at `hour` with the weather `w` (as a picture shows it: the sky asks 天候).
SkyLook LookWith(float hour, const Weather& w, double seconds = 0) {
    Sky s;
    s.kind = SkyKind::Earth;
    s.hour = hour;
    return LookOf(s, seconds, SkyWeatherOf(w, seconds));
}

}  // namespace

// The templates set the amounts at once (the wind's way stays); a value of a newer version: nothing.
RM_TEST(Weather_Presets) {
    Weather w;
    w.windDirection = 45;
    ApplyPreset(w, WeatherPreset::Storm);
    CHECK(w.cloudAmount == 100.f && w.rainAmount == 100.f && !w.snowing && w.lightning && w.windStrength > 15.f && w.windDirection == 45.f);
    ApplyPreset(w, WeatherPreset::Clear);
    CHECK(w.cloudAmount == 0.f && w.rainAmount == 0.f && !w.lightning && w.windStrength < 2.f);
    ApplyPreset(w, WeatherPreset::HeavySnow);
    CHECK(w.snowing && w.snowAmount == 80.f && w.rainAmount == 0.f);
    const Weather before = w;
    ApplyPreset(w, static_cast<WeatherPreset>(99));
    CHECK(w.cloudAmount == before.cloudAmount && w.snowAmount == before.snowAmount);
    // From light to heavy, the rain only grows; every template is one (12).
    float last = -1;
    for (const WeatherPreset p : {WeatherPreset::Sprinkle, WeatherPreset::Drizzle, WeatherPreset::Rain, WeatherPreset::Downpour, WeatherPreset::Storm}) {
        CHECK(Of(p).rainAmount > last);
        last = Of(p).rainAmount;
    }
    CHECK(reflect::TypeOf<Weather>().fields.size() > 0 && kWeatherPresets == 12);
    // The amounts are kept to tenths (the inspector shows "12.3"; the rest cut off).
    const reflect::FieldDesc* rain = nullptr;
    for (const reflect::FieldDesc& f : reflect::TypeOf<Weather>().fields) {
        if (std::string_view(f.name) == "rainAmount") rain = &f;
    }
    CHECK(rain && rain->decimals == 1 && reflect::SanitizeNumber(*rain, 33.37, 0) == 33.3 && reflect::FormatNumber(*rain, 33.37) == "33.3");
    CHECK(rain && reflect::SanitizeNumber(*rain, 120, 0) == 100);
}

// What the weather tells the sky: few clouds - fair (the sun hardly dimmed), many - cloudy; rain darkens
// them into rain clouds (and brings clouds however few were asked); snow's are paler and hazier.
RM_TEST(Weather_SkyLook) {
    const SkyLook noon = LookWith(12, Of(WeatherPreset::Fair));
    const SkyLook rain = LookWith(12, Of(WeatherPreset::Rain));
    CHECK(rain.lightStrength < noon.lightStrength * 0.5f);
    CHECK(rain.constants.param[1][0] > noon.constants.param[1][0]);
    Weather thin, thick;
    thin.cloudAmount = 10;
    thick.cloudAmount = 100;
    CHECK(LookWith(12, thick).constants.param[1][0] > LookWith(12, thin).constants.param[1][0]);
    CHECK(LookWith(12, thick).lightStrength < LookWith(12, thin).lightStrength);
    CHECK(LookWith(12, thin).lightStrength > noon.lightStrength * 0.95f);  // (few clouds: the sun as it is)
    Weather none;
    none.cloudAmount = 0;
    CHECK(Near(LookWith(12, none).constants.param[1][0], 0.f));
    CHECK(LookWith(12, Of(WeatherPreset::Cloudy)).lightStrength < noon.lightStrength * 0.7f);
    CHECK(rain.lightStrength < noon.lightStrength * 0.35f && rain.ambientSky.b < noon.ambientSky.b * 0.7f);
    // Rain darkens the clouds: the same clouds, more rain - darker; rain from a clear sky brings clouds.
    Weather grey = Of(WeatherPreset::Cloudy), raining = grey;
    raining.rainAmount = 60;
    CHECK(SkyWeatherOf(raining, 0).dark > SkyWeatherOf(grey, 0).dark + 0.2f);
    Weather sudden;
    sudden.cloudAmount = 0;
    sudden.rainAmount = 50;
    CHECK(SkyWeatherOf(sudden, 0).cover > 0.7f);
    // Snow: paler than rain's clouds, hazier.
    CHECK(SkyWeatherOf(Of(WeatherPreset::Snow), 0).dark < SkyWeatherOf(Of(WeatherPreset::Rain), 0).dark);
    CHECK(SkyWeatherOf(Of(WeatherPreset::Snow), 0).haze > SkyWeatherOf(Of(WeatherPreset::Cloudy), 0).haze);
    // The fair sky with no 天候 at all is about the same as 晴れ (20).
    Sky plain;
    plain.kind = SkyKind::Earth;
    plain.hour = 12;
    CHECK(Near(LookOf(plain, 0).lightStrength, noon.lightStrength, 0.05f));
    // What falls, as much as asked (/ 100); none without.
    CHECK(Near(RainNow(NowOf(Of(WeatherPreset::Rain))), 0.5f) && SnowNow(NowOf(Of(WeatherPreset::Rain))) == 0.f);
    CHECK(Near(SnowNow(NowOf(Of(WeatherPreset::Blizzard))), 1.f) && RainNow(NowOf(Of(WeatherPreset::Blizzard))) == 0.f);
    CHECK(RainNow(NowOf(thick)) == 0.f && SnowNow(NowOf(thick)) == 0.f);
    // 雪の量 falls only with 雪を降らせる.
    Weather off = Of(WeatherPreset::Snow);
    off.snowing = false;
    CHECK(SnowNow(NowOf(off)) == 0.f);
    // A fog only in a downpour (a few metres seen at 100) or a snowstorm.
    CHECK(FogOf(NowOf(Of(WeatherPreset::Rain))) == 0.f && FogOf(NowOf(Of(WeatherPreset::Fair))) == 0.f);
    const float storm = FogOf(NowOf(Of(WeatherPreset::Storm)));
    CHECK(storm > 0.4f && std::exp(-storm * 5.f) < 0.12f);  // (5 m: nearly gone)
    CHECK(FogOf(NowOf(Of(WeatherPreset::Downpour))) > 0.f && FogOf(NowOf(Of(WeatherPreset::Downpour))) < storm);
    CHECK(FogOf(NowOf(Of(WeatherPreset::Blizzard))) > 0.1f && FogOf(NowOf(Of(WeatherPreset::Blizzard))) < storm);
    // 濡れ before Play: as wet as the rain now makes things; dry without rain; snow a little.
    const std::vector<WetWay> wet = WetNow(Of(WeatherPreset::Rain));
    CHECK(wet.size() == 1 && Near(wet[0].sun, 1.f) && Near(wet[0].shade, 1.f) && wet[0].way.y < -0.9f);
    CHECK(WetNow(Of(WeatherPreset::Fair)).empty());
    Weather snow = Of(WeatherPreset::Snow);
    snow.windStrength = 0;
    CHECK(WetNow(snow).size() == 1 && Near(WetNow(snow)[0].sun, kSnowWet));  // (snow: a little, where it melts)
}

// 雷: now and then a flash, only while the time goes on (and only with many clouds).
RM_TEST(Weather_Lightning) {
    Weather storm = Of(WeatherPreset::Storm);
    int flashes = 0, dark = 0;
    for (int k = 1; k < 4000; ++k) {
        const game_x_3d::SkyWeather w = SkyWeatherOf(storm, k * 0.125);
        if (w.flash > 0) {
            ++flashes;
            CHECK(w.flash <= 1.f && LookWith(12, storm, k * 0.125).constants.param[1][3] == w.flash);
        } else {
            ++dark;
        }
    }
    CHECK(flashes > 20 && dark > flashes * 10);
    CHECK(SkyWeatherOf(storm, 0).flash == 0.f);
    Weather fair = Of(WeatherPreset::Fair);
    fair.lightning = true;
    bool any = false;
    for (int k = 1; k < 4000; ++k) any = any || SkyWeatherOf(fair, k * 0.125).flash > 0;
    CHECK(!any);
}

// A fade of the weather (scripts' rm::Sky::SetWeather ...): from the weather of the moment to the
// fields', eased; a fade begun during another starts where that one has come.
RM_TEST(Weather_Fade) {
    Weather sky = Of(WeatherPreset::Fair);
    CHECK(Near(NowOf(sky).clouds, 0.2f) && NowOf(sky).rain == 0.f);
    BeginFade(sky, 10);
    ApplyPreset(sky, WeatherPreset::Storm);
    CHECK(RainNow(NowOf(sky)) == 0.f && Near(NowOf(sky).clouds, 0.2f));  // not begun: still fair
    sky.fadeElapsed = 5;
    const WeatherNow half = NowOf(sky);
    CHECK(Near(half.clouds, 0.6f) && Near(half.rain, 0.5f));
    sky.fadeElapsed = 1;
    CHECK(NowOf(sky).rain < 0.1f);  // eased: slow at first
    sky.fadeElapsed = 5;
    const float midCover = SkyWeatherOf(sky, 0).cover;
    CHECK(midCover > SkyWeatherOf(Of(WeatherPreset::Fair), 0).cover && midCover <= SkyWeatherOf(Of(WeatherPreset::Storm), 0).cover);
    sky.fadeElapsed = 10;
    CHECK(Near(RainNow(NowOf(sky)), 1.f) && Near(NowOf(sky).clouds, 1.f));
    // A fade begun halfway through another starts from where it had come (no jump).
    sky.fadeElapsed = 5;
    const WeatherNow before = NowOf(sky);
    BeginFade(sky, 4);
    ApplyPreset(sky, WeatherPreset::Clear);
    const WeatherNow after = NowOf(sky);
    CHECK(Near(after.clouds, before.clouds) && Near(after.rain, before.rain));
    // 0 seconds: at once.
    Weather now = Of(WeatherPreset::Fair);
    BeginFade(now, 0);
    now.cloudAmount = 75;
    CHECK(Near(NowOf(now).clouds, 0.75f));
}

// 風: the direction (0 to the front -Z, + to the left) and strength as a vector; the clouds' drift and
// the drops' slant are it; a fade turns it the short way. While playing: the drift adds up, a fade goes
// on, the rain wets, 雪を降らせる comes in over about 10 seconds.
RM_TEST(Weather_Wind) {
    CHECK(Near(WindOf(0, 2), {0, 0, -2}));
    CHECK(Near(WindOf(90, 1), {-1, 0, 0}));
    CHECK(Near(WindOf(-90, 3), {3, 0, 0}));
    CHECK(Near(WindOf(45, 0), {0, 0, 0}) && Near(WindOf(45, -1), {0, 0, 0}));
    Weather sky = Of(WeatherPreset::Rain);
    sky.windDirection = -90;
    sky.windStrength = 10;
    const game_x_3d::SkyWeather sw = SkyWeatherOf(sky, 0);
    CHECK(Near(sw.windX, 10) && Near(sw.windZ, 0));
    CHECK(FallDirection(Precipitation::Kind::Rain, {sw.windX, 0, sw.windZ}).x > 0.3f);  // the drops slant with it
    Weather turn = Of(WeatherPreset::Fair);
    turn.windDirection = 350;
    turn.windStrength = 5;
    BeginFade(turn, 2);
    turn.windDirection = 10;
    turn.fadeElapsed = 1;
    const WeatherNow mid = NowOf(turn);
    CHECK(mid.windZ < -4.5f && std::fabs(mid.windX) < 0.1f);
    game_production::Scene scene;
    scene.SetSettingTypes({&reflect::TypeOf<Weather>()});
    Weather& s = *scene.Setting<Weather>();
    s.rainAmount = 50;
    s.windDirection = 90;
    s.windStrength = 4;
    game_production::SystemContext ctx{scene, 0.5f};
    RunWeather(ctx);
    CHECK(Near(s.driftX, -2.f) && Near(s.driftZ, 0.f));
    CHECK(s.wetCounted && s.wetWays.size() == 1 && Near(s.wetWays[0].sun, 0.011f));  // (Play starts dry: wetter as it rains - 0.5 s x (0.004 + 0.5 x 0.036))
    s.rainAmount = 0;
    RunWeather(ctx);
    CHECK(s.wetWays.size() == 1 && s.wetWays[0].sun < 0.011f && s.wetWays[0].sun > 0.004f);  // dries slowly
    CHECK(s.wetWays[0].shade > s.wetWays[0].sun);                                       // shade slower
    CHECK(WetNow(s).size() == 1 && Near(WetNow(s)[0].sun, s.wetWays[0].sun));
    BeginFade(s, 1);
    s.cloudAmount = 90;
    RunWeather(ctx);
    CHECK(Near(s.fadeElapsed, 0.5f));
    RunWeather(ctx);
    RunWeather(ctx);
    CHECK(Near(s.fadeElapsed, 1.f) && Near(NowOf(s).clouds, 0.9f));
    // 雪を降らせる: in over about 10 seconds (the editor shows it at once), out the same.
    Weather editor;
    editor.snowing = true;
    CHECK(SnowInOf(editor) == 1.f && Near(SnowNow(NowOf(editor)), 0.5f));
    s.snowing = true;
    RunWeather(ctx);
    CHECK(Near(s.snowIn, 0.05f) && Near(SnowNow(NowOf(s)), 0.025f));
    for (int k = 0; k < 30; ++k) RunWeather(ctx);
    CHECK(s.snowIn == 1.f && Near(SnowNow(NowOf(s)), 0.5f));
    s.snowing = false;
    RunWeather(ctx);
    CHECK(Near(s.snowIn, 0.95f) && SnowNow(NowOf(s)) > 0.f);
}

// 濡れ: the ways the rain fell - wetter quickly while it rains, what it reached stays wet while it
// rains (the wind turning starts another way), dry again slowly in game time, slower in shade.
// Snow wets a little where it melts - slowly, at most kSnowWet; nothing dries while it falls; rain and
// snow falling alike (no wind) are one way, as wet as the wetter makes it.
RM_TEST(Weather_SnowWets) {
    WeatherNow snowy;
    snowy.snow = 0.5f;
    std::vector<WetWay> s;
    StepWet(s, snowy, 1);  // (0.002 + 0.5 x 0.012 a second)
    CHECK(s.size() == 1 && Near(s[0].sun, 0.008f) && Near(s[0].way, {0, -1, 0}));
    CHECK(Near(s[0].seep, 0.0025f));  // (seeps under what covers: 0.001 + 0.5 x 0.003 m a second)
    StepWet(s, snowy, 1000);
    CHECK(Near(s[0].sun, kSnowWet) && Near(s[0].shade, kSnowWet) && Near(s[0].seep, kSeepSnow));
    std::vector<WetWay> rained = {{{0, -1, 0}, 1, 1}};
    StepWet(rained, snowy, 100);
    CHECK(Near(rained[0].sun, 1.f));  // (the rain's wet stays while it snows)
    CHECK(WetAtOnce(snowy).size() == 1 && Near(WetAtOnce(snowy)[0].sun, kSnowWet));
    WeatherNow both = snowy;
    both.rain = 1;
    CHECK(WetAtOnce(both).size() == 1 && Near(WetAtOnce(both)[0].sun, 1.f) && Near(WetAtOnce(both)[0].seep, kSeepRain));
    WeatherNow fair;
    StepWet(s, fair, 9);
    CHECK(Near(s[0].sun, kSnowWet - 0.1f));  // (dries as the rain's does)
}

// 積雪: deeper while it snows, up to 積もる高さ; melts after (faster in rain); the editor shows it lying.
RM_TEST(Weather_SnowCover) {
    WeatherNow snowy;
    snowy.snow = 1;
    float cover = StepSnowCover(0, snowy, 0.24f, 1);  // (0.24 x (0.1 + 0.9) / 240 a second)
    CHECK(Near(cover, 0.001f));
    cover = StepSnowCover(cover, snowy, 0.24f, 1000);
    CHECK(Near(cover, 0.24f));  // never above it
    CHECK(Near(StepSnowCover(cover, snowy, 0.1f, 1), 0.1f));  // (lowered: at once)
    WeatherNow light = snowy;
    light.snow = 0.5f;
    CHECK(Near(StepSnowCover(0, light, 0.24f, 1), 0.00055f));  // lighter snow: slower
    WeatherNow fair;
    CHECK(Near(StepSnowCover(0.24f, fair, 0.24f, 60), 0.228f));  // melts: about a centimetre a minute
    WeatherNow rain;
    rain.rain = 1;
    CHECK(Near(StepSnowCover(0.24f, rain, 0.24f, 60), 0.108f));  // faster in rain
    CHECK(StepSnowCover(0.01f, fair, 0.24f, 1000) == 0.f);
    CHECK(StepSnowCover(std::nanf(""), snowy, 0.24f, 1) >= 0.f && StepSnowCover(0, snowy, 99.f, 1e9f) == kMostSnowDepth);
    CHECK(StepSnowCover(0.1f, snowy, 0.24f, std::nanf("")) == 0.1f);
    CHECK(Near(SnowFallRate(snowy, 0.24f), 0.001f) && SnowFallRate(fair, 0.24f) == 0.f);  // (footprints fill up as fast)
    // The ways share it: what falls goes to the way it falls (slanted by the wind: a way of its own),
    // what melts goes from all alike.
    std::vector<WetWay> ways;
    StepWet(ways, snowy, 100, 0.24f);
    CHECK(ways.size() == 1 && Near(ways[0].snow, 0.1f) && Near(ways[0].way, {0, -1, 0}));
    WeatherNow windy = snowy;
    windy.windX = 3;
    StepWet(ways, windy, 50, 0.24f);
    CHECK(ways.size() == 2 && Near(ways[0].snow, 0.1f) && Near(ways[1].snow, 0.05f) && ways[1].way.x > 0.3f);
    StepWet(ways, windy, 1000, 0.24f);
    CHECK(Near(ways[0].snow + ways[1].snow, 0.24f));  // (all of it: never above 積もる高さ)
    const float before0 = ways[0].snow, before1 = ways[1].snow;
    StepWet(ways, fair, 60, 0.24f);
    CHECK(Near(ways[0].snow + ways[1].snow, 0.228f) && Near(ways[0].snow / before0, ways[1].snow / before1));
    StepWet(ways, fair, 100000, 0.24f);
    CHECK(ways.empty());  // (dry and melted: gone)
    // As a picture shows it: the editor - all of it lying, the way it falls; Play - as counted.
    Weather w;
    w.snowing = true;
    w.windStrength = 0;
    w.snowDepth = 0.3f;
    CHECK(Near(SnowCoverNow(w), 0.3f));
    CHECK(WetAtOnce(NowOf(w), w.snowDepth).size() == 1 && Near(WetAtOnce(NowOf(w), w.snowDepth)[0].snow, 0.3f));
    w.snowing = false;
    CHECK(SnowCoverNow(w) == 0.f);
    w.wetCounted = true;
    w.wetWays = {{{0, -1, 0}, 0, 0, 0, 0.05f}};
    CHECK(Near(SnowCoverNow(w), 0.05f));
    // A layer for a way: a grid across it around the eye in whole cells; every way's snow and map.
    const WetWay lying[2] = {{{0, -1, 0}, 0, 0, 0, 0.2f}, {space3d::Normalize(Vec3{1, -2, 0}), 0, 0, 0, 0.1f}};
    const int maps[2] = {3, 5};
    const SnowLayerConstants k = SnowLayerOf({10.1f, 3, -7.3f}, lying, maps, 1, 7, true);
    CHECK(Near(k.across[3], kSnowCell) && k.along[3] == static_cast<float>(kSnowCells) && k.way[3] == 1.f);
    CHECK(Near(Vec3{k.way[0], k.way[1], k.way[2]}, space3d::Normalize(Vec3{1, -2, 0})));
    const Vec3 a{k.across[0], k.across[1], k.across[2]}, b{k.along[0], k.along[1], k.along[2]}, d{k.way[0], k.way[1], k.way[2]};
    auto dot = [](Vec3 x, Vec3 y) { return x.x * y.x + x.y * y.y + x.z * y.z; };
    CHECK(std::fabs(dot(a, d)) < 1e-5f && std::fabs(dot(b, d)) < 1e-5f && std::fabs(dot(a, b)) < 1e-5f);
    const Vec3 middle{k.middle[0], k.middle[1], k.middle[2]};
    const float across = dot(middle, a) / kSnowCell;
    CHECK(std::fabs(across - std::round(across)) < 1e-3f);  // (whole cells: the grid does not swim)
    CHECK(Near(dot(middle, d), dot(Vec3{10.1f, 3, -7.3f}, d)));
    CHECK(k.snow[0][0] == 0.2f && k.snow[0][1] == 3.f && k.snow[1][0] == 0.1f && k.snow[1][1] == 5.f && k.snow[2][1] == -1.f);
    CHECK(k.trail[3] == static_cast<float>(kTrailCells) && k.trailMore[0] == 7.f);
    CHECK(SnowLayerOf({10.1f, 3, -7.3f}, lying, maps, 1, 7, false).trail[3] == 0.f);  // (no footprints in the editor)
    // The footprints' frame: around the eye in whole cells; the lines up start well under it.
    const SnowTrailConstants t = SnowTrailOf({10.1f, 3, -7.3f}, 7, 0.01f, 6);
    CHECK(Near(t.trail[0], std::floor(10.1f / kTrailCell) - kTrailCells / 2) && Near(t.trail[1], std::floor(-7.3f / kTrailCell) - kTrailCells / 2));
    CHECK(Near(t.trail[2], kTrailCell) && t.more[0] == 7.f && Near(t.more[1], 0.01f) && t.more[2] < 3.f - 50.f && t.more[3] == 6.f);
}

RM_TEST(Weather_Wetness) {
    WeatherNow rain;
    rain.rain = 0.25f;
    WeatherNow fair;
    CHECK(WetAtOnce(fair).empty() && WetAtOnce(rain).size() == 1 && Near(WetAtOnce(rain)[0].sun, 1.f));  // (the editor: wet through)
    CHECK(Near(WetAtOnce(rain)[0].seep, kSeepRain));  // (... and seeped as far as it goes)
    std::vector<WetWay> ways;
    StepWet(ways, rain, 1);  // (0.004 + 0.25 x 0.036 a second)
    CHECK(ways.size() == 1 && Near(ways[0].sun, 0.013f) && Near(ways[0].shade, 0.013f) && Near(ways[0].way, {0, -1, 0}));
    CHECK(Near(ways[0].seep, 0.004f));  // (seeps under what covers: 0.002 + 0.25 x 0.008 m a second)
    StepWet(ways, rain, 100);
    CHECK(Near(ways[0].sun, 1.f));  // all of it wet in the end, even in a light rain
    CHECK(Near(ways[0].seep, 0.404f));  // (still seeping further)
    StepWet(ways, rain, 1000);
    CHECK(Near(ways[0].seep, kSeepRain));
    WeatherNow downpour = rain;
    downpour.rain = 1;
    std::vector<WetWay> fast;
    StepWet(fast, downpour, 1);
    CHECK(fast[0].sun > 0.013f * 2);  // (sooner the harder it rains: 0.04 a second)
    // The wind turns: a way of its own; the first stays as wet as it was while it rains.
    WeatherNow windy = rain;
    windy.windX = 10;
    StepWet(ways, windy, 2);
    CHECK(ways.size() == 2 && Near(ways[0].sun, 1.f) && Near(ways[1].sun, 0.026f) && ways[1].way.x > 0.3f);
    // A little wind (within kWetWayDegrees): the same way.
    WeatherNow breeze = rain;
    breeze.windX = 0.5f;
    StepWet(ways, breeze, 1);
    CHECK(ways.size() == 2);
    // Less rain: nothing dries while it rains.
    WeatherNow drizzle = rain;
    drizzle.rain = 0.05f;
    StepWet(ways, drizzle, 50);
    CHECK(Near(ways[0].sun, 1.f) && Near(ways[1].sun, 0.026f));
    // It stops: dries in game time, shade slower; gone when dry.
    StepWet(ways, fair, 9);
    CHECK(Near(ways[0].sun, 0.9f) && Near(ways[0].shade, 0.97f) && Near(ways[0].seep, kSeepRain));  // (it seeped: kept while it dries)
    StepWet(ways, fair, 0);  // (paused: no time, no drying)
    CHECK(Near(ways[0].sun, 0.9f));
    StepWet(ways, fair, 100);
    CHECK(ways.size() == 1 && ways[0].sun == 0.f && ways[0].shade > 0.f);  // (the second way: dry in shade too, gone)
    StepWet(ways, fair, 1000);
    CHECK(ways.empty());
    // At most kWetWays: a new way takes the least wet one's place.
    for (int k = 0; k < 6; ++k) {
        WeatherNow w = rain;
        w.windX = static_cast<float>(k) * 6.f;
        StepWet(ways, w, 1);
    }
    CHECK(ways.size() == kWetWays);
    CHECK(Near(WetnessOf(ways), 0.026f) && WetnessOf({}) == 0.f);  // (24 and 30 m/s: within kWetWayDegrees - one way, wetter twice)
}

// What falls: how many for an amount, the way it goes in a wind, the numbers 濡れ's touch gets.
RM_TEST(Weather_Falling) {
    Precipitation snow;
    snow.kind = Precipitation::Kind::Snow;
    snow.amount = 1;
    CHECK(PrecipitationCount(snow) == kMaxSnowflakes);
    Precipitation rain;
    rain.amount = 0.5f;
    CHECK(PrecipitationCount(rain) == static_cast<uint32_t>(std::pow(0.5f, 1.4f) * kMaxRaindrops + 0.5f));  // (fewer than half: a little rain is a few drops)
    rain.amount = 0.05f;
    CHECK(PrecipitationCount(rain) < kMaxRaindrops / 50);
    rain.amount = 7;
    CHECK(PrecipitationCount(rain) == kMaxRaindrops);
    CHECK(Near(FallDirection(Precipitation::Kind::Rain, {0, 0, 0}), {0, -1, 0}));
    const Vec3 slant = FallDirection(Precipitation::Kind::Snow, {0, 0, -4});
    CHECK(slant.z < -0.7f && Near(slant.x * slant.x + slant.y * slant.y + slant.z * slant.z, 1.f));  // snow: light, far along
    // The surface's numbers: how many, then each way's sun / shade / map.
    const WetWay ways[2] = {{{0, -1, 0}, 0.5f, 0.75f, 0.6f}, {{1, 0, 0}, 2.f, 0.1f}};
    const int maps[2] = {3, -1};
    const std::vector<float> d = WetSurfaceData(ways, maps);
    CHECK(d.size() == 4 * (1 + 2 * kWetWays) && d[0] == 2.f);
    CHECK(d[4] == 0.5f && d[5] == 0.75f && d[6] == 3.f && d[7] == 0.6f && d[8] == 1.f && d[10] == -1.f && d[11] == 0.f);  // (clamped to 1; w: しみこみ)
    CHECK(d.size() <= 4 * space3d::kSurfaceData);
    CHECK(d.size() == 4 * (1 + 2 * kWetWays) && d[20] == 0.f);  // (each way's snow: none)
    const WetWay snowy[1] = {{{0, -1, 0}, 0, 0, 0, 0.25f}};
    CHECK(WetSurfaceData(snowy, maps)[20] == 0.25f);
}

// The shaders compile as they are drawn: the drops and flakes (with 3D 空間's 向きの地図), and the
// renderer's models with and without 濡れ's touch on every surface.
RM_TEST(Weather_ShadersCompile) {
    std::string error;
    const std::string fall = std::string(space3d::kColorHlsl) + space3d::kDirectionMapHlsl + kFallHlsl;
    CHECK(Compiles(fall, "FallVS", "vs_4_0", &error));
    CHECK(Compiles(fall, "FallPS", "ps_4_0", &error));
    if (!error.empty()) std::printf("%s\n", error.c_str());
    error.clear();
    const std::string plain = space3d::RendererShaderText();
    for (const char* entry : {"MeshPS", "FloorPS", "CopyDepthPS", "LinePS"}) CHECK(Compiles(plain, entry, "ps_4_0", &error));
    for (const char* entry : {"MeshVS", "ShadowVS", "ClearVS", "LineVS"}) CHECK(Compiles(plain, entry, "vs_4_0", &error));
    CHECK(Compiles(space3d::RendererShaderText(kWetSurfaceHlsl), "MeshPS", "ps_4_0", &error));
    const std::string layer = space3d::RendererShaderText(kWetSurfaceHlsl) + kSnowLayerHlsl;
    CHECK(Compiles(layer, "SnowLayerVS", "vs_4_0", &error));
    CHECK(Compiles(layer, "SnowLayerPS", "ps_4_0", &error));
    const std::string trail = space3d::RendererShaderText(kWetSurfaceHlsl) + kSnowTrailHlsl;
    CHECK(Compiles(trail, "SnowTrailVS", "vs_4_0", &error));
    CHECK(Compiles(trail, "SnowTrailPS", "ps_4_0", &error));
    if (!error.empty()) std::printf("%s\n", error.c_str());
    // A touch that does not compile does not compile (the renderer then keeps its own).
    CHECK(!Compiles(space3d::RendererShaderText("void Surface(inout float3 a) { a = ; }"), "MeshPS", "ps_4_0"));
}
