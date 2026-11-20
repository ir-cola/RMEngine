// プラグイン 天候 (weather/runtime) のテスト: 天候ごとに空・雷・フェード・風・濡れ・降るものがどう
// 決まるか、シェーダーがコンパイルできるか (D3DCompile、描くときと同じやり方) を調べる。3D 空間のも使う。

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

// 天候 `w` のときの `hour` 時の地球の空 (絵が見せるとおり。空は 天候 に尋ねる)。
SkyLook LookWith(float hour, const Weather& w, double seconds = 0) {
    Sky s;
    s.kind = SkyKind::Earth;
    s.hour = hour;
    return LookOf(s, seconds, SkyWeatherOf(w, seconds));
}

}  // namespace

// テンプレートは量をまとめて決める (風の向きは変えない)。新しい版の値は何もしない。
RM_TEST(Weather_Presets) {
    Weather w;
    w.windDirection = 45;
    ApplyPreset(w, WeatherPreset::Storm);
    CHECK(w.cloudAmount == 100.f && w.precipitation == 100.f && !w.snowing && w.lightning && w.windStrength > 15.f && w.windDirection == 45.f && w.fogAmount > 0.f);
    ApplyPreset(w, WeatherPreset::Clear);
    CHECK(w.cloudAmount == 0.f && w.precipitation == 0.f && !w.lightning && w.windStrength < 2.f && w.fogAmount == 0.f);
    ApplyPreset(w, WeatherPreset::Foggy);
    CHECK(w.fogAmount >= 50.f && w.precipitation == 0.f);
    ApplyPreset(w, WeatherPreset::HeavySnow);
    CHECK(w.snowing && w.precipitation == 80.f);
    const Weather before = w;
    ApplyPreset(w, static_cast<WeatherPreset>(99));
    CHECK(w.cloudAmount == before.cloudAmount && w.precipitation == before.precipitation);
    // 弱い雨から強い雨へ、雨は増えるだけ。どのテンプレートもそうなる (13)。
    float last = -1;
    for (const WeatherPreset p : {WeatherPreset::Sprinkle, WeatherPreset::Drizzle, WeatherPreset::Rain, WeatherPreset::Downpour, WeatherPreset::Storm}) {
        CHECK(Of(p).precipitation > last && !Of(p).snowing);
        last = Of(p).precipitation;
    }
    CHECK(reflect::TypeOf<Weather>().fields.size() > 0 && kWeatherPresets == 13);
    // 量は 10 分の 1 の単位で持つ (インスペクターは "12.3" と見せ、それ以下は切り捨てる)。
    const reflect::FieldDesc* rain = nullptr;
    for (const reflect::FieldDesc& f : reflect::TypeOf<Weather>().fields) {
        if (std::string_view(f.name) == "precipitation") rain = &f;
    }
    CHECK(rain && rain->decimals == 1 && reflect::SanitizeNumber(*rain, 33.37, 0) == 33.3 && reflect::FormatNumber(*rain, 33.37) == "33.3");
    CHECK(rain && reflect::SanitizeNumber(*rain, 120, 0) == 100);
}

// 天候が空に伝えること: 雲が少ないと快晴 (太陽はほとんど陰らない)、多いとくもり。雨が降ると雲が
// 暗くなって雨雲になる (雲が少なくても雲が出る)。雪の雲は白っぽく、かすんでいる。
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
    CHECK(LookWith(12, thin).lightStrength > noon.lightStrength * 0.95f);  // (雲が少ない: 太陽はそのまま)
    Weather none;
    none.cloudAmount = 0;
    CHECK(Near(LookWith(12, none).constants.param[1][0], 0.f));
    CHECK(LookWith(12, Of(WeatherPreset::Cloudy)).lightStrength < noon.lightStrength * 0.7f);
    CHECK(rain.lightStrength < noon.lightStrength * 0.35f && rain.ambientSky.b < noon.ambientSky.b * 0.7f);
    // 雨は雲を暗くする: 同じ雲で雨が多いほど暗い。晴れた空に雨を降らせると雲が出る。
    Weather grey = Of(WeatherPreset::Cloudy), raining = grey;
    raining.precipitation = 60;
    CHECK(SkyWeatherOf(raining, 0).dark > SkyWeatherOf(grey, 0).dark + 0.2f);
    Weather sudden;
    sudden.cloudAmount = 0;
    sudden.precipitation = 50;
    CHECK(SkyWeatherOf(sudden, 0).cover > 0.7f);
    // 雪: 雨の雲より白っぽく、かすんでいる。
    CHECK(SkyWeatherOf(Of(WeatherPreset::Snow), 0).dark < SkyWeatherOf(Of(WeatherPreset::Rain), 0).dark);
    CHECK(SkyWeatherOf(Of(WeatherPreset::Snow), 0).haze > SkyWeatherOf(Of(WeatherPreset::Cloudy), 0).haze);
    // 天候 が何もない快晴の空は 晴れ とほぼ同じ (20)。
    Sky plain;
    plain.kind = SkyKind::Earth;
    plain.hour = 12;
    CHECK(Near(LookOf(plain, 0).lightStrength, noon.lightStrength, 0.05f));
    // 降るもの: 頼まれた量だけ (/ 100)。頼まれなければ降らない。
    CHECK(Near(RainNow(NowOf(Of(WeatherPreset::Rain))), 0.5f) && SnowNow(NowOf(Of(WeatherPreset::Rain))) == 0.f);
    CHECK(Near(SnowNow(NowOf(Of(WeatherPreset::Blizzard))), 1.f) && RainNow(NowOf(Of(WeatherPreset::Blizzard))) == 0.f);
    CHECK(RainNow(NowOf(thick)) == 0.f && SnowNow(NowOf(thick)) == 0.f);
    // 降水量 は 1 つ: 雨、または 雪にする なら雪。両方は降らない。
    Weather off = Of(WeatherPreset::Snow);
    off.snowing = false;
    CHECK(SnowNow(NowOf(off)) == 0.f && Near(RainNow(NowOf(off)), 0.5f));
    for (int k = 0; k <= 100; ++k) {
        const float x = static_cast<float>(k) / 100.f;
        CHECK(std::min(RainShare(x), SnowShare(x)) == 0.f);  // (両方は降らない)
    }
    CHECK(RainShare(0) == 1.f && SnowShare(1) == 1.f && RainShare(0.49f) > 0.1f && RainShare(0.49f) < 0.2f && Near(SnowShare(0.5f), 0.15f));  // (霧雨、そして小雪)
    // 霧: 霧の量 (100 で数 m 先まで、50 で 20 m ほど先まで見える)。土砂降りや吹雪ではさらに濃い。
    CHECK(FogOf(NowOf(Of(WeatherPreset::Fair))) == 0.f);
    Weather misty;
    misty.fogAmount = 100;
    CHECK(Near(FogOf(NowOf(misty)), 0.5f) && std::exp(-FogOf(NowOf(misty)) * 5.f) < 0.1f);
    misty.fogAmount = 50;
    CHECK(std::exp(-FogOf(NowOf(misty)) * 20.f) < 0.1f && std::exp(-FogOf(NowOf(misty)) * 5.f) > 0.5f);
    Weather downpour = Of(WeatherPreset::Downpour), calm = downpour;
    calm.fogAmount = 0;
    CHECK(FogOf(NowOf(calm)) > 0.f && FogOf(NowOf(downpour)) > FogOf(NowOf(calm)));  // (自分の霧と、霧の量 も)
    CHECK(FogOf(NowOf(Of(WeatherPreset::Storm))) > FogOf(NowOf(downpour)));
    // 色: 薄い。雨では暗く、雪では白い。
    const Color light{1, 1, 1, 1};
    const Color fair = FogColorOf(NowOf(misty), light), rainy = FogColorOf(NowOf(downpour), light), snowy = FogColorOf(NowOf(Of(WeatherPreset::Snow)), light);
    CHECK(rainy.r < fair.r - 0.1f && snowy.r > fair.r + 0.1f && rainy.b > rainy.r);
    // Play の前の 濡れ: 今の雨が濡らす分だけ濡れる。雨がなければ乾いている。雪は少し濡らす。
    const std::vector<WetWay> wet = WetNow(Of(WeatherPreset::Rain));
    CHECK(wet.size() == 1 && Near(wet[0].sun, 1.f) && Near(wet[0].shade, 1.f) && wet[0].way.y < -0.9f);
    CHECK(WetNow(Of(WeatherPreset::Fair)).empty());
    Weather snow = Of(WeatherPreset::Snow);
    snow.windStrength = 0;
    CHECK(WetNow(snow).size() == 1 && Near(WetNow(snow)[0].sun, kSnowWet));  // (雪: 溶ける所で少し)
}

// 雷: ときどき光る。時間が進んでいるときだけ (雲が多いときだけ)。
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

// 天候のフェード (スクリプトの rm::Sky::SetWeather ...): その時点の天候から目標の天候へ、
// なめらかに変える。別のフェードの途中で始めたら、そのフェードが来た所から始める。
RM_TEST(Weather_Fade) {
    Weather sky = Of(WeatherPreset::Fair);
    CHECK(Near(NowOf(sky).clouds, 0.2f) && NowOf(sky).rain == 0.f);
    BeginFade(sky, 10);
    ApplyPreset(sky, WeatherPreset::Storm);
    CHECK(RainNow(NowOf(sky)) == 0.f && Near(NowOf(sky).clouds, 0.2f));  // (始まっていない: まだ快晴)
    sky.fadeElapsed = 5;
    const WeatherNow half = NowOf(sky);
    CHECK(Near(half.clouds, 0.6f) && Near(half.rain, 0.5f));
    sky.fadeElapsed = 1;
    CHECK(NowOf(sky).rain < 0.1f);  // (なめらか: 最初はゆっくり)
    sky.fadeElapsed = 5;
    const float midCover = SkyWeatherOf(sky, 0).cover;
    CHECK(midCover > SkyWeatherOf(Of(WeatherPreset::Fair), 0).cover && midCover <= SkyWeatherOf(Of(WeatherPreset::Storm), 0).cover);
    sky.fadeElapsed = 10;
    CHECK(Near(RainNow(NowOf(sky)), 1.f) && Near(NowOf(sky).clouds, 1.f));
    // 別のフェードの途中で始めたフェードは、そのフェードが来た所から始まる (飛ばない)。
    sky.fadeElapsed = 5;
    const WeatherNow before = NowOf(sky);
    BeginFade(sky, 4);
    ApplyPreset(sky, WeatherPreset::Clear);
    const WeatherNow after = NowOf(sky);
    CHECK(Near(after.clouds, before.clouds) && Near(after.rain, before.rain));
    // 0 秒: すぐに変わる。
    Weather now = Of(WeatherPreset::Fair);
    BeginFade(now, 0);
    now.cloudAmount = 75;
    CHECK(Near(NowOf(now).clouds, 0.75f));
}

// 風: 向き (0 で正面の -Z、+ で左) と強さをベクトルで持つ。雲の流れと雨粒の傾きは風で決まる。
// フェードは風を短いほうの回りで回す。Play 中は、流れが積み重なり、フェードが進み、雨が濡らし、
// 雪にする は約 10 秒かけて入ってくる。
RM_TEST(Weather_Wind) {
    CHECK(Near(WindOf(0, 2), {0, 0, 2}));  // (0: 前 = +Z)
    CHECK(Near(WindOf(90, 1), {-1, 0, 0}));
    CHECK(Near(WindOf(-90, 3), {3, 0, 0}));
    CHECK(Near(WindOf(45, 0), {0, 0, 0}) && Near(WindOf(45, -1), {0, 0, 0}));
    Weather sky = Of(WeatherPreset::Rain);
    sky.windDirection = -90;
    sky.windStrength = 10;
    const game_x_3d::SkyWeather sw = SkyWeatherOf(sky, 0);
    CHECK(Near(sw.windX, 10) && Near(sw.windZ, 0));
    CHECK(FallDirection(Precipitation::Kind::Rain, {sw.windX, 0, sw.windZ}).x > 0.3f);  // (雨粒が風で傾く)
    Weather turn = Of(WeatherPreset::Fair);
    turn.windDirection = 350;
    turn.windStrength = 5;
    BeginFade(turn, 2);
    turn.windDirection = 10;
    turn.fadeElapsed = 1;
    const WeatherNow mid = NowOf(turn);
    CHECK(mid.windZ > 4.5f && std::fabs(mid.windX) < 0.1f);
    game_production::Scene scene;
    scene.SetSettingTypes({&reflect::TypeOf<Weather>()});
    Weather& s = *scene.Setting<Weather>();
    s.precipitation = 50;
    s.windDirection = 90;
    s.windStrength = 4;
    game_production::SystemContext ctx{scene, 0.5f};
    RunWeather(ctx);
    CHECK(Near(s.driftX, -2.f) && Near(s.driftZ, 0.f));
    CHECK(s.wetCounted && s.wetWays.size() == 1 && Near(s.wetWays[0].sun, 0.011f));  // (Play は乾いた所から始まる: 雨が降るほど濡れる - 0.5 秒 x (0.004 + 0.5 x 0.036))
    s.precipitation = 0;
    RunWeather(ctx);
    CHECK(s.wetWays.size() == 1 && s.wetWays[0].sun < 0.011f && s.wetWays[0].sun > 0.004f);  // ゆっくり乾く
    CHECK(s.wetWays[0].shade > s.wetWays[0].sun);                                       // 日陰は遅い
    CHECK(WetNow(s).size() == 1 && Near(WetNow(s)[0].sun, s.wetWays[0].sun));
    BeginFade(s, 1);
    s.cloudAmount = 90;
    RunWeather(ctx);
    CHECK(Near(s.fadeElapsed, 0.5f));
    RunWeather(ctx);
    RunWeather(ctx);
    CHECK(Near(s.fadeElapsed, 1.f) && Near(NowOf(s).clouds, 0.9f));
    // 雪にする: 雨が弱まり、それから雪になる。約 5 秒かける (エディターはすぐに見せる)。戻すときも
    // 同じ道をたどる。
    Weather editor;
    editor.precipitation = 50;
    editor.snowing = true;
    CHECK(SnowInOf(editor) == 1.f && Near(SnowNow(NowOf(editor)), 0.5f) && RainNow(NowOf(editor)) == 0.f);
    s.precipitation = 50;
    s.snowing = true;
    RunWeather(ctx);
    CHECK(Near(s.snowIn, 0.1f) && RainNow(NowOf(s)) > 0.f && RainNow(NowOf(s)) < 0.5f && SnowNow(NowOf(s)) == 0.f);  // (雨が弱まる)
    for (int k = 0; k < 4; ++k) RunWeather(ctx);
    CHECK(Near(s.snowIn, 0.5f) && RainNow(NowOf(s)) == 0.f && Near(SnowNow(NowOf(s)), 0.075f));  // (今は小雪)
    for (int k = 0; k < 10; ++k) RunWeather(ctx);
    CHECK(s.snowIn == 1.f && Near(SnowNow(NowOf(s)), 0.5f) && RainNow(NowOf(s)) == 0.f);
    s.snowing = false;
    RunWeather(ctx);
    CHECK(Near(s.snowIn, 0.9f) && SnowNow(NowOf(s)) > 0.f && RainNow(NowOf(s)) == 0.f);
}

// 濡れ: 雨が降った方向ごとに持つ。雨の間は早く濡れる。雨が届いた所は雨の間は濡れたまま
// (風が回ると別の方向になる)。ゲーム内の時間でゆっくり乾き、日陰はもっと遅い。
// 雪は溶ける所を少しずつ濡らす (最大 kSnowWet)。降っている間は何も乾かない。雨と雪が同じように
// 降る (風なし) ときは 1 つの方向で、濡れの強いほうに合わせて濡れる。
RM_TEST(Weather_SnowWets) {
    WeatherNow snowy;
    snowy.snow = 0.5f;
    std::vector<WetWay> s;
    StepWet(s, snowy, 1);  // (1 秒に 0.002 + 0.5 x 0.012)
    CHECK(s.size() == 1 && Near(s[0].sun, 0.008f) && Near(s[0].way, {0, -1, 0}));
    CHECK(Near(s[0].seep, 0.0025f));  // (覆われた所の下へしみる: 1 秒に 0.001 + 0.5 x 0.003 m)
    StepWet(s, snowy, 1000);
    CHECK(Near(s[0].sun, kSnowWet) && Near(s[0].shade, kSnowWet) && Near(s[0].seep, kSeepSnow));
    std::vector<WetWay> rained = {{{0, -1, 0}, 1, 1}};
    StepWet(rained, snowy, 100);
    CHECK(Near(rained[0].sun, 1.f));  // (雪の間も雨の濡れは残る)
    CHECK(WetAtOnce(snowy).size() == 1 && Near(WetAtOnce(snowy)[0].sun, kSnowWet));
    WeatherNow both = snowy;
    both.rain = 1;
    CHECK(WetAtOnce(both).size() == 1 && Near(WetAtOnce(both)[0].sun, 1.f) && Near(WetAtOnce(both)[0].seep, kSeepRain));
    WeatherNow fair;
    StepWet(s, fair, 9);
    CHECK(Near(s[0].sun, kSnowWet - 0.1f));  // (雨のときと同じように乾く)
}

// 積雪: 雪が降る間は深くなり、積もる高さ まで。やむと溶ける (雨では速い)。エディターは積もった状態を見せる。
RM_TEST(Weather_SnowCover) {
    WeatherNow snowy;
    snowy.snow = 1;
    float cover = StepSnowCover(0, snowy, 0.24f, 60);  // (量 1: 1 分に 2.5 cm - 積もる高さに関係なく)
    CHECK(Near(cover, 0.025f) && Near(StepSnowCover(0, snowy, 1.f, 60), 0.025f));
    cover = StepSnowCover(cover, snowy, 0.24f, 1000);
    CHECK(Near(cover, 0.24f));  // それ以上にはならない
    CHECK(Near(StepSnowCover(cover, snowy, 0.1f, 1), 0.1f));  // (下げたら: すぐに)
    WeatherNow light = snowy;
    light.snow = 0.5f;
    CHECK(Near(StepSnowCover(0, light, 0.24f, 60), 0.0125f));  // 弱い雪: 降る量に比べて遅い
    WeatherNow fair;
    CHECK(Near(StepSnowCover(0.24f, fair, 0.24f, 60), 0.228f));  // 溶ける: 1 分に 1 cm ほど
    WeatherNow rain;
    rain.rain = 1;
    CHECK(Near(StepSnowCover(0.24f, rain, 0.24f, 60), 0.108f));  // 雨では速い
    CHECK(StepSnowCover(0.01f, fair, 0.24f, 1000) == 0.f);
    CHECK(StepSnowCover(std::nanf(""), snowy, 0.24f, 1) >= 0.f && StepSnowCover(0, snowy, 99.f, 1e9f) == kMostSnowDepth);
    CHECK(StepSnowCover(0.1f, snowy, 0.24f, std::nanf("")) == 0.1f);
    CHECK(Near(SnowFallRate(snowy), 0.025f / 60.f) && Near(SnowFallRate(light), 0.0125f / 60.f) && SnowFallRate(fair) == 0.f);  // (足跡も同じ速さで埋まる)
    // 方向ごとの層で分け合う: 降るものは降る方向に入る (風で斜めなら、その方向だけの層)。
    // 溶けるものはどの層からも同じだけ引く。
    std::vector<WetWay> ways;
    StepWet(ways, snowy, 240, 0.24f);
    CHECK(ways.size() == 1 && Near(ways[0].snow, 0.1f) && Near(ways[0].way, {0, -1, 0}));
    WeatherNow windy = snowy;
    windy.windX = 3;
    StepWet(ways, windy, 120, 0.24f);
    CHECK(ways.size() == 2 && Near(ways[0].snow, 0.1f) && Near(ways[1].snow, 0.05f) && ways[1].way.x > 0.3f);
    StepWet(ways, windy, 1000, 0.24f);
    CHECK(Near(ways[0].snow + ways[1].snow, 0.24f));  // (全部で: 積もる高さ を超えない)
    const float before0 = ways[0].snow, before1 = ways[1].snow;
    StepWet(ways, fair, 60, 0.24f);
    CHECK(Near(ways[0].snow + ways[1].snow, 0.228f) && Near(ways[0].snow / before0, ways[1].snow / before1));
    StepWet(ways, fair, 100000, 0.24f);
    CHECK(ways.empty());  // (乾いて溶けた: 消える)
    // 絵が見せるとおり: エディターは全部が積もった状態で降る方向のまま、Play は数えたとおり。
    Weather w;
    w.snowing = true;
    w.precipitation = 50;
    w.windStrength = 0;
    CHECK(w.snowDepthCm == 10.f);  // (最初は 10 cm: 足跡が分かる深さ)
    w.snowDepthCm = 34;  // (cm)
    CHECK(Near(SnowMostOf(w), 0.34f) && Near(SnowCoverNow(w), 0.34f));
    CHECK(WetAtOnce(NowOf(w), SnowMostOf(w)).size() == 1 && Near(WetAtOnce(NowOf(w), SnowMostOf(w))[0].snow, 0.34f));
    w.snowDepthCm = 5000;
    CHECK(SnowMostOf(w) == kMostSnowDepth);  // (多くて 10 m)
    w.snowDepthCm = -3;
    CHECK(SnowMostOf(w) == 0.f);
    w.snowDepthCm = 34;
    w.snowing = false;
    CHECK(SnowCoverNow(w) == 0.f);
    w.wetCounted = true;
    w.wetWays = {{{0, -1, 0}, 0, 0, 0, 0.05f}};
    CHECK(Near(SnowCoverNow(w), 0.05f));
    // 積もった雪: 目の真下に向けた 1 つの格子を、マスの整数個で。どの方向の雪も向きの地図も使う。
    const WetWay lying[2] = {{{0, -1, 0}, 0, 0, 0, 0.02f}, {space3d::Normalize(Vec3{1, -2, 0}), 0, 0, 0, 0.01f}};
    const int maps[2] = {3, 5};
    const SnowLayerConstants k = SnowLayerOf({10.1f, 3, -7.3f}, lying, maps, 6, 7, true);
    CHECK(Near(k.grid[2], kSnowCell) && k.grid[3] == static_cast<float>(kSnowCells) && k.info[0] == 6.f && Near(k.info[1], 3.f));
    CHECK(Near(k.grid[0] + kSnowCell * kSnowCells * 0.5f, std::floor(10.1f / kSnowCell) * kSnowCell));
    CHECK(Near(k.grid[1] + kSnowCell * kSnowCells * 0.5f, std::floor(-7.3f / kSnowCell) * kSnowCell));
    CHECK(SnowLayerOf({10.1f + kSnowCell * 0.1f, 3, -7.3f}, lying, maps, 6, 7, true).grid[0] == k.grid[0]);  // (格子は泳がない)
    CHECK(k.snow[0][0] == 0.02f && k.snow[0][1] == 3.f && k.snow[1][0] == 0.01f && k.snow[1][1] == 5.f && k.snow[2][1] == -1.f);
    CHECK(k.trail[3] == static_cast<float>(kTrailCells) && k.trailMore[0] == 7.f);
    CHECK(SnowLayerOf({10.1f, 3, -7.3f}, lying, maps, 6, 7, false).trail[3] == 0.f);  // (エディターに足跡はない)
    // 足跡の枠: 目のまわりをマスの整数個で。上向きの線は目よりかなり下から始まる。
    const SnowTrailConstants t = SnowTrailOf({10.1f, 3, -7.3f}, 7, 0.01f, 6);
    CHECK(Near(t.trail[0], std::floor(10.1f / kTrailCell) - kTrailCells / 2) && Near(t.trail[1], std::floor(-7.3f / kTrailCell) - kTrailCells / 2));
    CHECK(Near(t.trail[2], kTrailCell) && t.more[0] == 7.f && Near(t.more[1], 0.01f) && t.more[2] < 3.f - 50.f && t.more[3] == 6.f);
}

RM_TEST(Weather_Wetness) {
    WeatherNow rain;
    rain.rain = 0.25f;
    WeatherNow fair;
    CHECK(WetAtOnce(fair).empty() && WetAtOnce(rain).size() == 1 && Near(WetAtOnce(rain)[0].sun, 1.f));  // (エディター: しみ通るまで濡れる)
    CHECK(Near(WetAtOnce(rain)[0].seep, kSeepRain));  // (... 行けるところまでしみる)
    std::vector<WetWay> ways;
    StepWet(ways, rain, 1);  // (1 秒に 0.004 + 0.25 x 0.036)
    CHECK(ways.size() == 1 && Near(ways[0].sun, 0.013f) && Near(ways[0].shade, 0.013f) && Near(ways[0].way, {0, -1, 0}));
    CHECK(Near(ways[0].seep, 0.004f));  // (覆われた所の下へしみる: 1 秒に 0.002 + 0.25 x 0.008 m)
    StepWet(ways, rain, 100);
    CHECK(Near(ways[0].sun, 1.f));  // 弱い雨でも、最後は全部が濡れる
    CHECK(Near(ways[0].seep, 0.404f));  // (まだ奥へしみている)
    StepWet(ways, rain, 1000);
    CHECK(Near(ways[0].seep, kSeepRain));
    WeatherNow downpour = rain;
    downpour.rain = 1;
    std::vector<WetWay> fast;
    StepWet(fast, downpour, 1);
    CHECK(fast[0].sun > 0.013f * 2);  // (強い雨ほど早い: 1 秒に 0.04)
    // 風が回る: 別の方向になる。最初の方向は、雨の間は濡れたままになる。
    WeatherNow windy = rain;
    windy.windX = 10;
    StepWet(ways, windy, 2);
    CHECK(ways.size() == 2 && Near(ways[0].sun, 1.f) && Near(ways[1].sun, 0.026f) && ways[1].way.x > 0.3f);
    // 少しの風 (kWetWayDegrees 以内): 同じ方向。
    WeatherNow breeze = rain;
    breeze.windX = 0.5f;
    StepWet(ways, breeze, 1);
    CHECK(ways.size() == 2);
    // 雨が弱まる: 降っている間は何も乾かない。
    WeatherNow drizzle = rain;
    drizzle.rain = 0.05f;
    StepWet(ways, drizzle, 50);
    CHECK(Near(ways[0].sun, 1.f) && Near(ways[1].sun, 0.026f));
    // やむ: ゲーム内の時間で乾く。日陰は遅い。乾いたら消える。
    StepWet(ways, fair, 9);
    CHECK(Near(ways[0].sun, 0.9f) && Near(ways[0].shade, 0.97f) && Near(ways[0].seep, kSeepRain));  // (しみた分: 乾く間は残る)
    StepWet(ways, fair, 0);  // (止まっている: 時間が進まず、乾かない)
    CHECK(Near(ways[0].sun, 0.9f));
    StepWet(ways, fair, 100);
    CHECK(ways.size() == 1 && ways[0].sun == 0.f && ways[0].shade > 0.f);  // (2 つ目の方向: 日陰も乾いて消える)
    StepWet(ways, fair, 1000);
    CHECK(ways.empty());
    // 多くて kWetWays: 新しい方向は、いちばん濡れていない方向の場所に入る。
    for (int k = 0; k < 6; ++k) {
        WeatherNow w = rain;
        w.windX = static_cast<float>(k) * 6.f;
        StepWet(ways, w, 1);
    }
    CHECK(ways.size() == kWetWays);
    CHECK(Near(WetnessOf(ways), 0.026f) && WetnessOf({}) == 0.f);  // (24 と 30 m/s: kWetWayDegrees 以内 - 方向は 1 つで、濡れは 2 倍)
}

// 降るもの: 量から数、風でのたどる向き、濡れ が受け取る数。
RM_TEST(Weather_Falling) {
    Precipitation snow;
    snow.kind = Precipitation::Kind::Snow;
    snow.amount = 1;
    CHECK(PrecipitationCount(snow) == kMaxSnowflakes);
    Precipitation rain;
    rain.amount = 0.5f;
    CHECK(PrecipitationCount(rain) == static_cast<uint32_t>(std::pow(0.5f, 1.4f) * kMaxRaindrops + 0.5f));  // (半分より少ない: 弱い雨は数粒)
    rain.amount = 0.05f;
    CHECK(PrecipitationCount(rain) < kMaxRaindrops / 50);
    rain.amount = 7;
    CHECK(PrecipitationCount(rain) == kMaxRaindrops);
    CHECK(Near(FallDirection(Precipitation::Kind::Rain, {0, 0, 0}), {0, -1, 0}));
    const Vec3 slant = FallDirection(Precipitation::Kind::Snow, {0, 0, -4});
    CHECK(slant.z < -0.7f && Near(slant.x * slant.x + slant.y * slant.y + slant.z * slant.z, 1.f));  // 雪: 軽く、遠くまで流れる
    // 面に渡す数: まず数、それから方向ごとの sun / shade / 地図。
    const WetWay ways[2] = {{{0, -1, 0}, 0.5f, 0.75f, 0.6f}, {{1, 0, 0}, 2.f, 0.1f}};
    const int maps[2] = {3, -1};
    const std::vector<float> d = WetSurfaceData(ways, maps);
    CHECK(d.size() == 4 * (1 + 2 * kWetWays) && d[0] == 2.f);
    CHECK(d[4] == 0.5f && d[5] == 0.75f && d[6] == 3.f && d[7] == 0.6f && d[8] == 1.f && d[10] == -1.f && d[11] == 0.f);  // (clamped to 1; w: しみこみ)
    CHECK(d.size() <= 4 * space3d::kSurfaceData);
    CHECK(d.size() == 4 * (1 + 2 * kWetWays) && d[20] == 0.f);  // (方向ごとの雪: なし)
    const WetWay snowy[1] = {{{0, -1, 0}, 0, 0, 0, 0.25f}};
    CHECK(WetSurfaceData(snowy, maps)[20] == 0.25f);
}

// シェーダーは描くときと同じやり方でコンパイルする: 雨粒と雪片 (3D 空間の 向きの地図 を使う) と、
// ランタイムのモデルを、どの面でも 濡れ の部分あり・なしの両方で。
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
    // コンパイルできない部分は、コンパイルできない (そのときランタイムは自分のを使い続ける)。
    CHECK(!Compiles(space3d::RendererShaderText("void Surface(inout float3 a) { a = ; }"), "MeshPS", "ps_4_0"));
}
