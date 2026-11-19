// Tests for the scene's 空 (game_x_3d/runtime/Sky.h) and the sky's shaders (space3d/runtime/SkyShader.h):
// where the sun is by the hour, what the weather and the night do to the light, the light that follows
// the sky, the clock while playing, and that every sky - the built-in ones and a new .rmsky - compiles
// (D3DCompile, as the renderer does), with a user's mistake named by its file and line.

#include "Test.h"

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/space3d/runtime/SkyShader.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

using namespace rm;
using namespace rm::game_x_3d;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool Near(Vec3 a, Vec3 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

// The renderer's way: D3DCompile with strictness (ps_4_0 / vs_4_0). The compiler's messages in *error.
bool Compiles(const std::string& hlsl, const char* entry, const char* target, std::string* error = nullptr) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(hlsl.data(), hlsl.size(), "sky", nullptr, nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    if (FAILED(hr) && error && errors) *error = static_cast<const char*>(errors->GetBufferPointer());
    return SUCCEEDED(hr);
}

Sky Earth(float hour, Weather weather = Weather::Clear) {
    Sky s;
    s.kind = SkyKind::Earth;
    s.hour = hour;
    s.weather = weather;
    return s;
}

}  // namespace

RM_TEST(Sky_SunByTheHour) {
    Sky s = Earth(6);
    s.sunHeight = 60;
    CHECK(Near(SunDirection(s), {1, 0, 0}));  // up in the east
    s.hour = 12;
    CHECK(Near(SunDirection(s), {0, std::sin(60 * 3.14159265f / 180), std::cos(60 * 3.14159265f / 180)}));  // high, on the +Z side
    s.hour = 18;
    CHECK(Near(SunDirection(s), {-1, 0, 0}));  // down in the west
    s.hour = 0;
    CHECK(SunDirection(s).y < -0.8f);  // as low below
    s.hour = 24;
    CHECK(Near(SunDirection(s), SunDirection(Earth(0))));
    // 太陽の向き turns the whole path (90: the sun rises in front, -Z).
    s.hour = 6;
    s.sunDirection = 90;
    CHECK(Near(SunDirection(s), {0, 0, -1}));
    // Not 地球の空: where its sun is at noon, whatever the hour.
    Sky space;
    space.kind = SkyKind::Space;
    space.hour = 3;
    space.sunHeight = 90;
    CHECK(Near(SunDirection(space), {0, 1, 0}));
}

RM_TEST(Sky_LookOfDayAndNight) {
    // Noon, clear: the sun's light (white, full), the light goes down from it, blue above.
    const SkyLook noon = LookOf(Earth(12), 5);
    CHECK(noon.builtin == space3d::BuiltinSky::Earth);
    CHECK(noon.drivesLight);
    const Vec3 sun = SunDirection(Earth(12));
    CHECK(Near(noon.lightGoes, {-sun.x, -sun.y, -sun.z}));
    CHECK(noon.lightStrength > 0.9f && noon.lightColor.b > 0.8f);
    CHECK(noon.constants.param[2][2] > noon.constants.param[2][0]);  // the zenith is blue
    CHECK(noon.constants.param[0][3] > 0.99f);                       // day
    CHECK(Near(noon.constants.sun[3], 5) && Near(noon.constants.moon[3], 12));
    // Midnight: the moon's faint blue light, stars.
    const SkyLook night = LookOf(Earth(0), 0);
    CHECK(night.lightStrength < 0.2f && night.lightStrength > 0.f);
    CHECK(night.lightColor.b > night.lightColor.r);
    CHECK(night.lightGoes.y < 0);              // from the moon, above
    CHECK(night.constants.param[2][3] > 0.8f);  // the stars (a few clouds even when clear)
    CHECK(night.ambientSky.b < noon.ambientSky.b);
    // Evening: red light low.
    const SkyLook evening = LookOf(Earth(17.8f), 0);
    CHECK(evening.lightColor.r > evening.lightColor.b);
    CHECK(evening.constants.param[4][3] > 0.f);  // the sunset's glow
    // Weather: less sunlight, more cloud - the more cloudy, the more so.
    const SkyLook rain = LookOf(Earth(12, Weather::Rain), 0);
    CHECK(rain.lightStrength < noon.lightStrength * 0.5f);
    CHECK(rain.constants.param[1][0] > noon.constants.param[1][0]);
    Sky thin = Earth(12, Weather::Cloudy), thick = thin;
    thin.cloudiness = 0.1f;
    thick.cloudiness = 1.f;
    CHECK(LookOf(thick, 0).constants.param[1][0] > LookOf(thin, 0).constants.param[1][0]);
    CHECK(LookOf(thick, 0).lightStrength < LookOf(thin, 0).lightStrength);
    CHECK(Near(LookOf(Earth(12), 0).constants.param[1][0], 0.15f));  // 晴れ: a few clouds, as before (its own 雲の量: 0.2)
    // 晴れ's own 雲の量: none to many, the sun hardly dimmed - and always less cloud, more sun than くもり.
    Sky fair = Earth(12), cloudyFair = fair, few = Earth(12, Weather::Cloudy);
    fair.clearClouds = 0;
    cloudyFair.clearClouds = 1;
    few.cloudiness = 0;
    CHECK(Near(LookOf(fair, 0).constants.param[1][0], 0.f) && LookOf(cloudyFair, 0).constants.param[1][0] > 0.6f);
    CHECK(LookOf(cloudyFair, 0).lightStrength > noon.lightStrength * 0.75f);
    CHECK(LookOf(few, 0).constants.param[1][0] >= LookOf(cloudyFair, 0).constants.param[1][0] * 0.5f);
    CHECK(LookOf(few, 0).lightStrength < LookOf(cloudyFair, 0).lightStrength);
    // くもり / 雨 darker than before (2026-11-19): little sunlight, the sky's own light dimmer.
    CHECK(LookOf(Earth(12, Weather::Cloudy), 0).lightStrength < noon.lightStrength * 0.45f);
    CHECK(rain.lightStrength < noon.lightStrength * 0.25f && rain.ambientSky.b < noon.ambientSky.b * 0.6f);
    // 雨 / 雪: what falls (none in clear or cloudy weather), as much as asked, lit.
    CHECK(rain.rain.amount == 0.5f && rain.rain.kind == space3d::Precipitation::Kind::Rain && rain.snow.amount == 0.f);
    CHECK(rain.rain.light.r > 0.f);
    Sky snow = Earth(12, Weather::Snow);
    snow.snow = 1;
    CHECK(LookOf(snow, 3).snow.kind == space3d::Precipitation::Kind::Snow && LookOf(snow, 3).snow.amount == 1.f &&
          Near(LookOf(snow, 3).snow.seconds, 3) && LookOf(snow, 3).rain.amount == 0.f);
    CHECK(LookOf(thick, 0).rain.amount == 0.f && LookOf(thick, 0).snow.amount == 0.f && noon.rain.amount == 0.f && noon.snow.amount == 0.f);
    CHECK(space3d::PrecipitationCount(LookOf(snow, 0).snow) == space3d::kMaxSnowflakes);
    CHECK(space3d::PrecipitationCount(rain.rain) == space3d::kMaxRaindrops / 2);
    // 濡れ before Play: as wet as the rain now makes things (twice its amount, at most 1); dry without rain.
    CHECK(Near(rain.wetness, 1.f) && noon.wetness == 0.f && LookOf(snow, 0).wetness == 0.f);
    // 雷: now and then a flash, only while the time goes on (and only in cloudy or rainy weather).
    Sky storm = Earth(12, Weather::Rain);
    storm.lightning = true;
    int flashes = 0, dark = 0;
    for (int k = 1; k < 4000; ++k) {
        const SkyLook l = LookOf(storm, k * 0.125);
        if (l.flash > 0) {
            ++flashes;
            CHECK(l.flash <= 1.f && l.constants.param[1][3] == l.flash);
        } else {
            ++dark;
        }
    }
    CHECK(flashes > 20 && dark > flashes * 10);
    CHECK(LookOf(storm, 0).flash == 0.f);
    Sky snowStorm = Earth(12, Weather::Snow);
    snowStorm.lightning = true;
    bool any = false;
    for (int k = 1; k < 4000; ++k) any = any || LookOf(snowStorm, k * 0.125).flash > 0;
    CHECK(!any);
    // Brightness; 光を空に合わせる off.
    Sky dim = Earth(12);
    dim.brightness = 0.5f;
    dim.driveLight = false;
    CHECK(Near(LookOf(dim, 0).constants.info[0], 0.5f) && !LookOf(dim, 0).drivesLight);
    // The others.
    Sky none;
    CHECK(!LookOf(none, 0).builtin && !LookOf(none, 0).drivesLight);
    Sky pano;
    pano.kind = SkyKind::Panorama;
    pano.turn = 90;
    CHECK(LookOf(pano, 0).builtin == space3d::BuiltinSky::Panorama && Near(LookOf(pano, 0).constants.param[0][0], 3.14159265f / 2));
    Sky custom;
    custom.kind = SkyKind::Custom;
    custom.value = {1, 2, 3};
    custom.color = {0.5f, 0.25f, 1, 1};
    const SkyLook c = LookOf(custom, 0);
    CHECK(!c.builtin && !c.drivesLight && c.constants.param[0][2] == 3.f && c.constants.param[1][1] == 0.25f);
    Sky space;
    space.kind = SkyKind::Space;
    space.nebulaColor1 = {1, 0, 0, 1};
    CHECK(LookOf(space, 0).builtin == space3d::BuiltinSky::Space && LookOf(space, 0).constants.param[1][0] == 1.f && LookOf(space, 0).drivesLight);
}

// A fade of the weather (scripts' rm::Sky): from the weather of the moment to the fields' weather,
// eased; a fade begun during another starts where that one has come; both falls while rain turns to snow.
RM_TEST(Sky_WeatherFade) {
    Sky sky = Earth(12, Weather::Clear);
    // No fade: the fields as they are.
    CHECK(NowOf(sky).weight[0] == 1.f && NowOf(sky).weight[2] == 0.f);
    BeginFade(sky, 10);
    sky.weather = Weather::Rain;
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
    const float midCover = LookOf(sky, 0).constants.param[1][0];
    Sky fair = Earth(12), wet = Earth(12, Weather::Rain);
    wet.rain = 1;
    CHECK(midCover > LookOf(fair, 0).constants.param[1][0] && midCover < LookOf(wet, 0).constants.param[1][0]);
    CHECK(LookOf(sky, 0).rain.amount > 0.f && LookOf(sky, 0).rain.amount < 1.f);
    // Done: the fields' weather.
    sky.fadeElapsed = 10;
    CHECK(NowOf(sky).weight[2] == 1.f && Near(RainNow(NowOf(sky)), 1.f));
    // A fade begun halfway through another starts from where it had come (no jump).
    sky.fadeElapsed = 5;
    const WeatherNow before = NowOf(sky);
    BeginFade(sky, 4);
    sky.weather = Weather::Snow;
    const WeatherNow after = NowOf(sky);
    CHECK(Near(after.weight[0], before.weight[0]) && Near(after.weight[2], before.weight[2]) && after.weight[3] == 0.f);
    // Rain into snow: both fall on the way.
    Sky turning = Earth(12, Weather::Rain);
    BeginFade(turning, 2);
    turning.weather = Weather::Snow;
    turning.fadeElapsed = 1;
    const SkyLook both = LookOf(turning, 0);
    CHECK(both.rain.amount > 0.f && both.snow.amount > 0.f);
    // 0 seconds: at once.
    Sky now = Earth(12);
    BeginFade(now, 0);
    now.weather = Weather::Cloudy;
    CHECK(NowOf(now).weight[1] == 1.f);
}

// 風: the direction (0 to the front -Z, + to the left) and strength as a vector; the clouds' drift and
// the drops' slant are it; a fade turns it the short way.
RM_TEST(Sky_Wind) {
    CHECK(Near(WindOf(0, 2), {0, 0, -2}));
    CHECK(Near(WindOf(90, 1), {-1, 0, 0}));
    CHECK(Near(WindOf(-90, 3), {3, 0, 0}));
    CHECK(Near(WindOf(45, 0), {0, 0, 0}) && Near(WindOf(45, -1), {0, 0, 0}));
    Sky sky = Earth(12, Weather::Rain);
    sky.windDirection = -90;
    sky.windStrength = 10;
    const SkyLook look = LookOf(sky, 0);
    CHECK(Near(look.wind, {10, 0, 0}) && Near(look.constants.param[5][2], 10));
    // From 350 to 10 degrees: through 0, not round the back.
    Sky turn = Earth(12);
    turn.windDirection = 350;
    turn.windStrength = 5;
    BeginFade(turn, 2);
    turn.windDirection = 10;
    turn.fadeElapsed = 1;
    const WeatherNow mid = NowOf(turn);
    CHECK(mid.windZ < -4.5f && std::fabs(mid.windX) < 0.1f);
    // While playing the wind carries them: the drift adds up (and a fade goes on, and the rain wets).
    game_production::Scene scene;
    scene.SetSettingTypes({&reflect::TypeOf<Sky>()});
    Sky& s = *scene.Setting<Sky>();
    s.kind = SkyKind::Earth;
    s.weather = Weather::Rain;
    s.rain = 0.5f;
    s.windDirection = 90;
    s.windStrength = 4;
    game_production::SystemContext ctx{scene, 0.5f};
    RunSkyClock(ctx);
    CHECK(Near(s.driftX, -2.f) && Near(s.driftZ, 0.f));
    CHECK(Near(s.wetness, 1.f));  // (raining from the start: wet from the start)
    s.weather = Weather::Clear;
    RunSkyClock(ctx);
    CHECK(s.wetness < 1.f && s.wetness > 0.9f);  // dries slowly
    CHECK(Near(LookOf(s, 0).wetness, s.wetness));
    BeginFade(s, 1);
    s.weather = Weather::Snow;
    RunSkyClock(ctx);
    CHECK(Near(s.fadeElapsed, 0.5f));
    RunSkyClock(ctx);
    RunSkyClock(ctx);
    CHECK(Near(s.fadeElapsed, 1.f) && NowOf(s).weight[3] == 1.f);
}

// 濡れ: wetter quickly while it rains, dry again slowly.
RM_TEST(Sky_Wetness) {
    WeatherNow rain;
    rain.weight[0] = 0;
    rain.weight[2] = 1;
    rain.rain = 0.25f;
    WeatherNow fair;
    CHECK(Near(NextWetness(-1, rain, 0), 0.5f) && NextWetness(-1, fair, 0) == 0.f);
    CHECK(Near(NextWetness(0, rain, 1), 1.f / 8.f));
    CHECK(Near(NextWetness(0.4f, rain, 100), 0.5f));  // never past what the rain makes
    CHECK(Near(NextWetness(1, fair, 9), 0.9f));
    CHECK(NextWetness(0.05f, fair, 100) == 0.f);
}

RM_TEST(Sky_FollowSky) {
    const SkyLook look = LookOf(Earth(9), 0);
    // No light: a directional one is added (first: it may cast the sun's shadows).
    std::vector<space3d::Light> lights;
    space3d::Light bulb;
    bulb.type = space3d::LightType::Point;
    lights.push_back(bulb);
    space3d::Ambient ambient;
    ambient.intensity = 2;
    FollowSky(look, lights, ambient);
    CHECK(lights.size() == 2 && lights[0].type == space3d::LightType::Directional);
    CHECK(Near(lights[0].direction, look.lightGoes) && Near(lights[0].intensity, look.lightStrength));
    CHECK(ambient.sky.b == look.ambientSky.b && ambient.intensity == 2);  // the colours follow; the strength stays
    // The first directional light: its 強さ is a multiplier; the others are left alone.
    std::vector<space3d::Light> two(2);
    two[0].intensity = 2;
    two[1].direction = {1, 0, 0};
    FollowSky(look, two, ambient);
    CHECK(two.size() == 2 && Near(two[0].intensity, 2 * look.lightStrength) && Near(two[1].direction, {1, 0, 0}));
    // Not driving: nothing.
    Sky off = Earth(9);
    off.driveLight = false;
    std::vector<space3d::Light> none;
    FollowSky(LookOf(off, 0), none, ambient);
    CHECK(none.empty());
}

RM_TEST(Sky_ClockWhilePlaying) {
    game_production::Scene scene;
    scene.SetSettingTypes({&reflect::TypeOf<Sky>()});
    Sky& sky = *scene.Setting<Sky>();
    sky.kind = SkyKind::Earth;
    sky.hour = 23;
    game_production::SystemContext ctx{scene, 1.f};
    RunSkyClock(ctx);
    CHECK(sky.hour == 23);  // 1 日の長さ 0: it stays
    sky.dayLength = 24;     // an hour a second
    RunSkyClock(ctx);
    CHECK(Near(sky.hour, 0));  // round past midnight
    ctx.dt = 0.5f;
    RunSkyClock(ctx);
    CHECK(Near(sky.hour, 0.5f));
    sky.kind = SkyKind::Space;  // only 地球の空 has a time
    RunSkyClock(ctx);
    CHECK(Near(sky.hour, 0.5f));
}

RM_TEST(Sky_ShadersCompile) {
    using space3d::BuiltinSky;
    for (const BuiltinSky b : {BuiltinSky::Earth, BuiltinSky::Space, BuiltinSky::Panorama, BuiltinSky::SixFaces}) {
        const std::string hlsl = space3d::AssembleSky(space3d::BuiltinSkyCode(b), space3d::BuiltinSkyName(b));
        std::string error;
        CHECK(Compiles(hlsl, "SkyPS", "ps_4_0", &error));
        if (!error.empty()) std::printf("%s: %s\n", space3d::BuiltinSkyName(b), error.c_str());
        CHECK(Compiles(hlsl, "SkyVS", "vs_4_0"));
    }
    // A new .rmsky as it is made.
    CHECK(Compiles(space3d::AssembleSky(space3d::NewSkyCode(), "NewSky.rmsky"), "SkyPS", "ps_4_0"));
    // A mistake: the message names the file and its own line (the engine's part around it is not counted).
    std::string error;
    const std::string broken = "float3 Sky(SkyInput i) {\n    float3 c = i.dir;\n    return c +;\n}\n";
    CHECK(!Compiles(space3d::AssembleSky(broken, "my \"sky\".rmsky"), "SkyPS", "ps_4_0", &error));
    CHECK(error.find("my _sky_.rmsky(3") != std::string::npos);
    // No Sky function at all: it does not compile (the engine's part asks for it).
    CHECK(!Compiles(space3d::AssembleSky("float3 Other() { return 0; }", "x.rmsky"), "SkyPS", "ps_4_0"));
    // The constants as the GPU reads them.
    static_assert(sizeof(space3d::SkyConstants) == (16 + 4 * 3 + 4 * space3d::kSkyParams) * sizeof(float));
}
