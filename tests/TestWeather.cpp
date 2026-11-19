// Tests for the plugin 天候 (weather/runtime): what each weather tells the sky, lightning, a fade, the
// wind, 濡れ, what falls, and that its shaders compile (D3DCompile, as it is drawn) - with 3D 空間's.

#include "Test.h"

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/space3d/runtime/RendererShaders.h"
#include "rm_engine/space3d/runtime/SkyShader.h"
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

Weather Of(WeatherKind kind) {
    Weather w;
    w.kind = kind;
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

// What each weather tells the sky: less sunlight, more cloud - the more cloudy, the more so; 晴れ has
// its own 雲の量 (the sun hardly dimmed).
RM_TEST(Weather_SkyLook) {
    const SkyLook noon = LookWith(12, Of(WeatherKind::Clear));
    Weather rainy = Of(WeatherKind::Rain);
    const SkyLook rain = LookWith(12, rainy);
    CHECK(rain.lightStrength < noon.lightStrength * 0.5f);
    CHECK(rain.constants.param[1][0] > noon.constants.param[1][0]);
    Weather thin = Of(WeatherKind::Cloudy), thick = thin;
    thin.cloudiness = 0.1f;
    thick.cloudiness = 1.f;
    CHECK(LookWith(12, thick).constants.param[1][0] > LookWith(12, thin).constants.param[1][0]);
    CHECK(LookWith(12, thick).lightStrength < LookWith(12, thin).lightStrength);
    CHECK(Near(noon.constants.param[1][0], 0.15f));  // 晴れ: a few clouds (its own 雲の量: 0.2) - as with no 天候
    Weather fair = Of(WeatherKind::Clear), cloudyFair = fair, few = Of(WeatherKind::Cloudy);
    fair.clearClouds = 0;
    cloudyFair.clearClouds = 1;
    few.cloudiness = 0;
    CHECK(Near(LookWith(12, fair).constants.param[1][0], 0.f) && LookWith(12, cloudyFair).constants.param[1][0] > 0.6f);
    CHECK(LookWith(12, cloudyFair).lightStrength > noon.lightStrength * 0.75f);
    CHECK(LookWith(12, few).constants.param[1][0] >= LookWith(12, cloudyFair).constants.param[1][0] * 0.5f);
    CHECK(LookWith(12, few).lightStrength < LookWith(12, cloudyFair).lightStrength);
    CHECK(LookWith(12, Of(WeatherKind::Cloudy)).lightStrength < noon.lightStrength * 0.45f);
    CHECK(rain.lightStrength < noon.lightStrength * 0.25f && rain.ambientSky.b < noon.ambientSky.b * 0.6f);
    // The fair sky with no 天候 at all is the same as 晴れ's 0.2.
    Sky plain;
    plain.kind = SkyKind::Earth;
    plain.hour = 12;
    CHECK(Near(LookOf(plain, 0).constants.param[1][0], noon.constants.param[1][0]) && Near(LookOf(plain, 0).lightStrength, noon.lightStrength, 0.01f));
    // 雨 / 雪: what falls (none in clear or cloudy weather), as much as asked.
    CHECK(Near(RainNow(NowOf(rainy)), 0.5f) && SnowNow(NowOf(rainy)) == 0.f);
    Weather snow = Of(WeatherKind::Snow);
    snow.snow = 1;
    CHECK(SnowNow(NowOf(snow)) == 1.f && RainNow(NowOf(snow)) == 0.f);
    CHECK(RainNow(NowOf(thick)) == 0.f && SnowNow(NowOf(thick)) == 0.f);
    // 濡れ before Play: as wet as the rain now makes things (twice its amount, at most 1); dry without rain.
    const std::vector<WetWay> wet = WetNow(rainy);
    CHECK(wet.size() == 1 && Near(wet[0].sun, 1.f) && Near(wet[0].shade, 1.f) && wet[0].way.y < -0.9f);
    CHECK(WetNow(Of(WeatherKind::Clear)).empty() && WetNow(snow).empty());
}

// 雷: now and then a flash, only while the time goes on (and only in cloudy or rainy weather).
RM_TEST(Weather_Lightning) {
    Weather storm = Of(WeatherKind::Rain);
    storm.lightning = true;
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
    Weather snowStorm = Of(WeatherKind::Snow);
    snowStorm.lightning = true;
    bool any = false;
    for (int k = 1; k < 4000; ++k) any = any || SkyWeatherOf(snowStorm, k * 0.125).flash > 0;
    CHECK(!any);
}

// A fade of the weather (scripts' rm::Sky::SetWeather ...): from the weather of the moment to the fields' weather,
// eased; a fade begun during another starts where that one has come; both falls while rain turns to snow.
RM_TEST(Weather_Fade) {
    Weather sky = Of(WeatherKind::Clear);
    // No fade: the fields as they are.
    CHECK(NowOf(sky).weight[0] == 1.f && NowOf(sky).weight[2] == 0.f);
    BeginFade(sky, 10);
    sky.kind = WeatherKind::Rain;
    sky.rain = 1;
    CHECK(NowOf(sky).weight[0] == 1.f && RainNow(NowOf(sky)) == 0.f);  // not begun: still fair
    sky.fadeElapsed = 5;
    const WeatherNow half = NowOf(sky);
    CHECK(Near(half.weight[0], 0.5f) && Near(half.weight[2], 0.5f) && Near(half.weight[0] + half.weight[1] + half.weight[2] + half.weight[3], 1.f));
    CHECK(Near(half.rain, 0.75f) && Near(RainNow(half), 0.375f));  // (its share x the amount on its way from 0.5 to 1)
    // Eased: slow at first.
    sky.fadeElapsed = 1;
    CHECK(NowOf(sky).weight[2] < 0.1f);
    // The look in between: between the two weathers' looks.
    sky.fadeElapsed = 5;
    const float midCover = SkyWeatherOf(sky, 0).cover;
    Weather fair = Of(WeatherKind::Clear), wet = Of(WeatherKind::Rain);
    wet.rain = 1;
    CHECK(midCover > SkyWeatherOf(fair, 0).cover && midCover < SkyWeatherOf(wet, 0).cover);
    CHECK(RainNow(NowOf(sky)) > 0.f && RainNow(NowOf(sky)) < 1.f);
    // Done: the fields' weather.
    sky.fadeElapsed = 10;
    CHECK(NowOf(sky).weight[2] == 1.f && Near(RainNow(NowOf(sky)), 1.f));
    // A fade begun halfway through another starts from where it had come (no jump).
    sky.fadeElapsed = 5;
    const WeatherNow before = NowOf(sky);
    BeginFade(sky, 4);
    sky.kind = WeatherKind::Snow;
    const WeatherNow after = NowOf(sky);
    CHECK(Near(after.weight[0], before.weight[0]) && Near(after.weight[2], before.weight[2]) && after.weight[3] == 0.f);
    // Rain into snow: both fall on the way.
    Weather turning = Of(WeatherKind::Rain);
    BeginFade(turning, 2);
    turning.kind = WeatherKind::Snow;
    turning.fadeElapsed = 1;
    CHECK(RainNow(NowOf(turning)) > 0.f && SnowNow(NowOf(turning)) > 0.f);
    // 0 seconds: at once.
    Weather now = Of(WeatherKind::Clear);
    BeginFade(now, 0);
    now.kind = WeatherKind::Cloudy;
    CHECK(NowOf(now).weight[1] == 1.f);
}

// 風: the direction (0 to the front -Z, + to the left) and strength as a vector; the clouds' drift and
// the drops' slant are it; a fade turns it the short way.
RM_TEST(Weather_Wind) {
    CHECK(Near(WindOf(0, 2), {0, 0, -2}));
    CHECK(Near(WindOf(90, 1), {-1, 0, 0}));
    CHECK(Near(WindOf(-90, 3), {3, 0, 0}));
    CHECK(Near(WindOf(45, 0), {0, 0, 0}) && Near(WindOf(45, -1), {0, 0, 0}));
    Weather sky = Of(WeatherKind::Rain);
    sky.windDirection = -90;
    sky.windStrength = 10;
    const game_x_3d::SkyWeather sw = SkyWeatherOf(sky, 0);
    CHECK(Near(sw.windX, 10) && Near(sw.windZ, 0));
    CHECK(FallDirection(Precipitation::Kind::Rain, {sw.windX, 0, sw.windZ}).x > 0.3f);  // the drops slant with it
    // From 350 to 10 degrees: through 0, not round the back.
    Weather turn = Of(WeatherKind::Clear);
    turn.windDirection = 350;
    turn.windStrength = 5;
    BeginFade(turn, 2);
    turn.windDirection = 10;
    turn.fadeElapsed = 1;
    const WeatherNow mid = NowOf(turn);
    CHECK(mid.windZ < -4.5f && std::fabs(mid.windX) < 0.1f);
    // While playing the wind carries them: the drift adds up (and a fade goes on, and the rain wets).
    game_production::Scene scene;
    scene.SetSettingTypes({&reflect::TypeOf<Weather>()});
    Weather& s = *scene.Setting<Weather>();
    s.kind = WeatherKind::Rain;
    s.rain = 0.5f;
    s.windDirection = 90;
    s.windStrength = 4;
    game_production::SystemContext ctx{scene, 0.5f};
    RunWeather(ctx);
    CHECK(Near(s.driftX, -2.f) && Near(s.driftZ, 0.f));
    CHECK(s.wetCounted && s.wetWays.size() == 1 && Near(s.wetWays[0].sun, 0.025f));  // (Play starts dry: wetter as it rains - 0.5 s x (0.01 + 0.5 x 0.08))
    s.kind = WeatherKind::Clear;
    RunWeather(ctx);
    CHECK(s.wetWays.size() == 1 && s.wetWays[0].sun < 0.025f && s.wetWays[0].sun > 0.015f);  // dries slowly
    CHECK(s.wetWays[0].shade > s.wetWays[0].sun);                                       // shade slower
    CHECK(WetNow(s).size() == 1 && Near(WetNow(s)[0].sun, s.wetWays[0].sun));
    BeginFade(s, 1);
    s.kind = WeatherKind::Snow;
    RunWeather(ctx);
    CHECK(Near(s.fadeElapsed, 0.5f));
    RunWeather(ctx);
    RunWeather(ctx);
    CHECK(Near(s.fadeElapsed, 1.f) && NowOf(s).weight[3] == 1.f);
}

// 濡れ: the ways the rain fell - wetter quickly while it rains, what it reached stays wet while it
// rains (the wind turning starts another way), dry again slowly in game time, slower in shade.
RM_TEST(Weather_Wetness) {
    WeatherNow rain;
    rain.weight[0] = 0;
    rain.weight[2] = 1;
    rain.rain = 0.25f;
    WeatherNow fair;
    CHECK(WetAtOnce(fair).empty() && WetAtOnce(rain).size() == 1 && Near(WetAtOnce(rain)[0].sun, 1.f));  // (the editor: wet through)
    std::vector<WetWay> ways;
    StepWet(ways, rain, 1);  // (0.01 + 0.25 x 0.08 a second)
    CHECK(ways.size() == 1 && Near(ways[0].sun, 0.03f) && Near(ways[0].shade, 0.03f) && Near(ways[0].way, {0, -1, 0}));
    StepWet(ways, rain, 100);
    CHECK(Near(ways[0].sun, 1.f));  // all of it wet in the end, even in a light rain
    WeatherNow downpour = rain;
    downpour.rain = 1;
    std::vector<WetWay> fast;
    StepWet(fast, downpour, 1);
    CHECK(fast[0].sun > 0.03f * 2);  // (sooner the harder it rains: 0.09 a second)
    // The wind turns: a way of its own; the first stays as wet as it was while it rains.
    WeatherNow windy = rain;
    windy.windX = 10;
    StepWet(ways, windy, 2);
    CHECK(ways.size() == 2 && Near(ways[0].sun, 1.f) && Near(ways[1].sun, 0.06f) && ways[1].way.x > 0.3f);
    // A little wind (within kWetWayDegrees): the same way.
    WeatherNow breeze = rain;
    breeze.windX = 0.5f;
    StepWet(ways, breeze, 1);
    CHECK(ways.size() == 2);
    // Less rain: nothing dries while it rains.
    WeatherNow drizzle = rain;
    drizzle.rain = 0.05f;
    StepWet(ways, drizzle, 50);
    CHECK(Near(ways[0].sun, 1.f) && Near(ways[1].sun, 0.06f));
    // It stops: dries in game time, shade slower; gone when dry.
    StepWet(ways, fair, 9);
    CHECK(Near(ways[0].sun, 0.9f) && Near(ways[0].shade, 0.97f));
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
    CHECK(Near(WetnessOf(ways), 0.06f) && WetnessOf({}) == 0.f);  // (24 and 30 m/s: within kWetWayDegrees - one way, wetter twice)
}

// What falls: how many for an amount, the way it goes in a wind, the numbers 濡れ's touch gets.
RM_TEST(Weather_Falling) {
    Precipitation snow;
    snow.kind = Precipitation::Kind::Snow;
    snow.amount = 1;
    CHECK(PrecipitationCount(snow) == kMaxSnowflakes);
    Precipitation rain;
    rain.amount = 0.5f;
    CHECK(PrecipitationCount(rain) == kMaxRaindrops / 2);
    rain.amount = 7;
    CHECK(PrecipitationCount(rain) == kMaxRaindrops);
    CHECK(Near(FallDirection(Precipitation::Kind::Rain, {0, 0, 0}), {0, -1, 0}));
    const Vec3 slant = FallDirection(Precipitation::Kind::Snow, {0, 0, -4});
    CHECK(slant.z < -0.7f && Near(slant.x * slant.x + slant.y * slant.y + slant.z * slant.z, 1.f));  // snow: light, far along
    // The surface's numbers: how many, then each way's sun / shade / map.
    const WetWay ways[2] = {{{0, -1, 0}, 0.5f, 0.75f}, {{1, 0, 0}, 2.f, 0.1f}};
    const int maps[2] = {3, -1};
    const std::vector<float> d = WetSurfaceData(ways, maps);
    CHECK(d.size() == 4 * (1 + kWetWays) && d[0] == 2.f);
    CHECK(d[4] == 0.5f && d[5] == 0.75f && d[6] == 3.f && d[8] == 1.f && d[10] == -1.f);  // (clamped to 1)
    CHECK(d.size() <= 4 * space3d::kSurfaceData);
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
    if (!error.empty()) std::printf("%s\n", error.c_str());
    // A touch that does not compile does not compile (the renderer then keeps its own).
    CHECK(!Compiles(space3d::RendererShaderText("void Surface(inout float3 a) { a = ; }"), "MeshPS", "ps_4_0"));
}
