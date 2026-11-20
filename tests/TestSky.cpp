// シーンの 空 (game_x_3d/runtime/Sky.h) と空のシェーダー (space3d/runtime/SkyShader.h) のテスト:
// 時刻で決まる太陽の位置、天気と夜が光に与えること、空に付いてくる光、Play 中の時計、そして
// どの空も - 組み込みのものも新しい .rmsky も - (描画側と同じ D3DCompile で) コンパイルできること。
// 利用者の間違いは、そのファイルと行で示す。

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

// 描画側のやり方: 厳しさを付けた D3DCompile (ps_4_0 / vs_4_0)。
// コンパイラのメッセージは *error に入る。
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
    CHECK(Near(SunDirection(s), {1, 0, 0}));  // 東の空に昇っている
    s.hour = 12;
    CHECK(Near(SunDirection(s), {0, std::sin(60 * 3.14159265f / 180), std::cos(60 * 3.14159265f / 180)}));  // 高く、+Z の側
    s.hour = 18;
    CHECK(Near(SunDirection(s), {-1, 0, 0}));  // 西に沈んでいる
    s.hour = 0;
    CHECK(SunDirection(s).y < -0.8f);  // 同じだけ下
    s.hour = 24;
    CHECK(Near(SunDirection(s), SunDirection(Earth(0))));
    // 太陽の向き は道すじ全体を回す (90: 太陽は正面の -Z から昇る)。
    s.hour = 6;
    s.sunDirection = 90;
    CHECK(Near(SunDirection(s), {0, 0, -1}));
    // 地球の空 ではない: 時刻に関係なく、正午の太陽の位置。
    Sky space;
    space.kind = SkyKind::Space;
    space.hour = 3;
    space.sunHeight = 90;
    CHECK(Near(SunDirection(space), {0, 1, 0}));
}

RM_TEST(Sky_LookOfDayAndNight) {
    // 正午、快晴: 太陽の光 (白、全開)、光はそこから下へ向かい、上は青。
    const SkyLook noon = LookOf(Earth(12), 5);
    CHECK(noon.builtin == space3d::BuiltinSky::Earth);
    CHECK(noon.drivesLight);
    const Vec3 sun = SunDirection(Earth(12));
    CHECK(Near(noon.lightGoes, {-sun.x, -sun.y, -sun.z}));
    CHECK(noon.lightStrength > 0.9f && noon.lightColor.b > 0.8f);
    CHECK(noon.constants.param[2][2] > noon.constants.param[2][0]);  // 天頂は青
    CHECK(noon.constants.param[0][3] > 0.99f);                       // 昼
    CHECK(Near(noon.constants.sun[3], 5) && Near(noon.constants.moon[3], 12));
    // 真夜中: 月の弱い青い光、星。
    const SkyLook night = LookOf(Earth(0), 0);
    CHECK(night.lightStrength < 0.2f && night.lightStrength > 0.f);
    CHECK(night.lightColor.b > night.lightColor.r);
    CHECK(night.lightGoes.y < 0);              // 月から、上にある
    CHECK(night.constants.param[2][3] > 0.8f);  // 星 (快晴でも雲が少しある)
    CHECK(night.ambientSky.b < noon.ambientSky.b);
    // 夕方: 低い赤い光。
    const SkyLook evening = LookOf(Earth(17.8f), 0);
    CHECK(evening.lightColor.r > evening.lightColor.b);
    CHECK(evening.constants.param[4][3] > 0.f);  // 夕焼けの輝き
    // 天気が伝えること (プラグイン 天候: SkyWeather) - 雲、通ってくる日光、稲光、風。
    // 何も伝えない: 雲が少しの晴れた空 (TestWeather.cpp: 各天気が伝えること)。
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
    // レジストリが無い、または何も伝えない: 晴れた空。
    game_production::Scene empty;
    CHECK(Near(SkyWeatherOf(nullptr, empty, 0).cover, 0.15f));
    // Brightness; 光を空に合わせる off.
    Sky dim = Earth(12);
    dim.brightness = 0.5f;
    dim.driveLight = false;
    CHECK(Near(LookOf(dim, 0).constants.info[0], 0.5f) && !LookOf(dim, 0).drivesLight);
    // そのほか。
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
    // 光が無い: 平行光を 1 つ足す (先頭に: 太陽の影を落とすかもしれない)。
    std::vector<space3d::Light> lights;
    space3d::Light bulb;
    bulb.type = space3d::LightType::Point;
    lights.push_back(bulb);
    space3d::Ambient ambient;
    ambient.intensity = 2;
    FollowSky(look, lights, ambient);
    CHECK(lights.size() == 2 && lights[0].type == space3d::LightType::Directional);
    CHECK(Near(lights[0].direction, look.lightGoes) && Near(lights[0].intensity, look.lightStrength));
    CHECK(ambient.sky.b == look.ambientSky.b && ambient.intensity == 2);  // 色は付いてくる。強さはそのまま
    // 最初の平行光: その 強さ は倍率になる。ほかには触らない。
    std::vector<space3d::Light> two(2);
    two[0].intensity = 2;
    two[1].direction = {1, 0, 0};
    FollowSky(look, two, ambient);
    CHECK(two.size() == 2 && Near(two[0].intensity, 2 * look.lightStrength) && Near(two[1].direction, {1, 0, 0}));
    // 動かさない: 何もしない。
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
    sky.dayLength = 24;     // 1 秒で 1 時間
    RunSkyClock(ctx);
    CHECK(Near(sky.hour, 0));  // 真夜中をまわって戻る
    ctx.dt = 0.5f;
    RunSkyClock(ctx);
    CHECK(Near(sky.hour, 0.5f));
    sky.kind = SkyKind::Space;  // 地球の空 だけが時刻を持つ
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
    // 作ったままの新しい .rmsky。
    CHECK(Compiles(space3d::AssembleSky(space3d::NewSkyCode(), "NewSky.rmsky"), "SkyPS", "ps_4_0"));
    // 間違い: メッセージにそのファイルと、そのファイル自身の行を示す
    // (周りのエンジンの部分は数えない)。
    std::string error;
    const std::string broken = "float3 Sky(SkyInput i) {\n    float3 c = i.dir;\n    return c +;\n}\n";
    CHECK(!Compiles(space3d::AssembleSky(broken, "my \"sky\".rmsky"), "SkyPS", "ps_4_0", &error));
    CHECK(error.find("my _sky_.rmsky(3") != std::string::npos);
    // Sky 関数がまったく無い: コンパイルできない (エンジンの部分がそれを求める)。
    CHECK(!Compiles(space3d::AssembleSky("float3 Other() { return 0; }", "x.rmsky"), "SkyPS", "ps_4_0"));
    // GPU が読むとおりの定数。
    static_assert(sizeof(space3d::SkyConstants) == (16 + 4 * 3 + 4 * space3d::kSkyParams) * sizeof(float));
}
