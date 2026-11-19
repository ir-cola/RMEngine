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

Sky Earth(float hour) {
    Sky s;
    s.kind = SkyKind::Earth;
    s.hour = hour;
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
    // What the weather tells (the plugin 天候: SkyWeather) - its clouds, the sunlight through, a flash,
    // the wind; none told: a fair sky with a few clouds (TestWeather.cpp: what each weather tells).
    CHECK(Near(noon.constants.param[1][0], 0.15f) && noon.flash == 0.f && noon.constants.param[5][2] == 0.f);
    SkyWeather grey;
    grey.cover = 0.9f;
    grey.dark = 0.6f;
    grey.through = 0.2f;
    grey.flash = 0.7f;
    grey.driftX = 3;
    grey.windZ = -2;
    const SkyLook g = LookOf(Earth(12), 0, grey);
    CHECK(Near(g.constants.param[1][0], 0.9f) && Near(g.constants.param[1][1], 0.6f) && Near(g.constants.param[1][3], 0.7f) && Near(g.flash, 0.7f));
    CHECK(g.lightStrength < noon.lightStrength * 0.25f && g.ambientSky.b < noon.ambientSky.b);
    CHECK(Near(g.constants.param[5][0], 3) && Near(g.constants.param[5][3], -2));
    // No registry, or none that tells: the fair sky.
    game_production::Scene empty;
    CHECK(Near(SkyWeatherOf(nullptr, empty, 0).cover, 0.15f));
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
