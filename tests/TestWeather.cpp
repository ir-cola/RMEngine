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
#include "rm_engine/weather/runtime/Lightning.h"
#include "rm_engine/weather/runtime/RainClouds.h"
#include "rm_engine/weather/runtime/Weather.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
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
    // Play の前 (と止めた後) の 濡れ: 雨でも雪でも無い (Play の跡を残さない - 2026-11-22)。
    CHECK(WetNow(Of(WeatherPreset::Rain)).empty() && WetNow(Of(WeatherPreset::Fair)).empty() && WetNow(Of(WeatherPreset::Snow)).empty());
    CHECK(SnowCoverNow(Of(WeatherPreset::Snow)) == 0.f);
}

// 稲妻 (Lightning.h、2026-11-26): 空の閃光と同じ時に起き、光ってから kBoltLife 秒で消える。明るさは強く光る →
// 暗く → もう一度 → 残光。形は雲の下から地面までつながった幹と、細く暗い枝。場所はカメラから遠すぎず近すぎない。
// 描いた雨雲の上空の雨雲のかたまり: 降る所の上だけ・降る量で段と暗さと濃さ・ワールドに留まる・遠い順。
RM_TEST(Weather_RainCloudPuffs) {
    constexpr int n = 8;
    constexpr float cell = 24.f;
    PlaceClouds map;
    map.firstX = 0, map.firstZ = 0;
    map.rains.assign(n * n, 0.f);
    map.rains[3 * n + 2] = 1.f;    // 嵐 (x 48, z 72)
    map.rains[5 * n + 5] = 0.2f;   // 小雨 (x 120, z 120)
    std::vector<CloudPuff> puffs;
    RainCloudPuffs(map, n, cell, {0, 0, 0}, puffs);
    CHECK(puffs.size() == 4);  // 嵐 3 段 + 小雨 1 段
    int storm = 0, drizzle = 0;
    float stormShade = 0, drizzleShade = 1, stormAlpha = 0, drizzleAlpha = 1;
    for (const CloudPuff& p : puffs) {
        CHECK(p.center.y >= kRainCloudBase && p.radius > 0);
        if (std::fabs(p.center.x - 48) <= 0.35f * cell + 1e-3f && std::fabs(p.center.z - 72) <= 0.35f * cell + 1e-3f) {
            ++storm;
            stormShade = std::max(stormShade, p.shade);
            stormAlpha = std::max(stormAlpha, p.alpha);
        } else if (std::fabs(p.center.x - 120) <= 0.35f * cell + 1e-3f && std::fabs(p.center.z - 120) <= 0.35f * cell + 1e-3f) {
            ++drizzle;
            drizzleShade = p.shade;
            drizzleAlpha = p.alpha;
        }
    }
    CHECK(storm == 3 && drizzle == 1);
    CHECK(stormShade > drizzleShade + 0.5f && stormAlpha > drizzleAlpha + 0.4f);  // 嵐は黒ずんで濃い、小雨は明るく薄い
    // 遠い順。
    auto dist = [](const CloudPuff& p) { return p.center.x * p.center.x + p.center.y * p.center.y + p.center.z * p.center.z; };
    for (size_t k = 1; k < puffs.size(); ++k) CHECK(dist(puffs[k - 1]) >= dist(puffs[k]));
    // 地図がずれても (カメラが動いても)、同じ所の雲は同じ。
    PlaceClouds moved;
    moved.firstX = -cell, moved.firstZ = 0;
    moved.rains.assign(n * n, 0.f);
    moved.rains[3 * n + 3] = 1.f;  // (同じ x 48)
    std::vector<CloudPuff> again;
    RainCloudPuffs(moved, n, cell, {0, 0, 0}, again);
    CHECK(again.size() == 3);
    for (const CloudPuff& p : again) {
        CHECK(std::any_of(puffs.begin(), puffs.end(), [&](const CloudPuff& q) {
            return q.seed == p.seed && std::fabs(q.center.x - p.center.x) < 1e-3f && std::fabs(q.center.z - p.center.z) < 1e-3f;
        }));
    }
    // 降っていない・遠すぎる・数が合わない: 置かない。
    PlaceClouds distant;
    distant.firstX = kRainCloudReach + 100, distant.firstZ = 0;
    distant.rains.assign(n * n, 1.f);
    RainCloudPuffs(distant, n, cell, {0, 0, 0}, again);
    CHECK(again.empty());
    map.rains.pop_back();
    RainCloudPuffs(map, n, cell, {0, 0, 0}, again);
    CHECK(again.empty());
    // カメラを向いた正方形: 1 つにつき頂点 6 個、角は 真ん中 ± 右 x 半径 ± 上 x 半径。
    std::vector<PuffVertex> quads;
    PuffQuads(puffs, {1, 0, 0}, {0, 1, 0}, quads);
    CHECK(quads.size() == puffs.size() * 6);
    const CloudPuff& p0 = puffs[0];
    CHECK(std::fabs(quads[0].position.x - (p0.center.x - p0.radius)) < 1e-3f && std::fabs(quads[0].position.y - (p0.center.y - p0.radius)) < 1e-3f);
    CHECK(std::fabs(quads[2].position.x - (p0.center.x + p0.radius)) < 1e-3f && quads[2].u == 1.f && quads[2].v == 1.f);
    // シェーダーがコンパイルできる。
    CHECK(Compiles(kRainPuffHlsl, "PuffVS", "vs_4_0") && Compiles(kRainPuffHlsl, "PuffPS", "ps_4_0"));
}

RM_TEST(Weather_LightningBolt) {
    // 起きる時: 空が光っている時は、いつも稲妻も見えている (同じ数から決まる)。
    Weather storm = Of(WeatherPreset::Storm);
    CHECK(LightningOn(storm) && !LightningOn(Of(WeatherPreset::Clear)));
    int strikes = 0;
    for (int k = 1; k < 4000; ++k) {
        const double t = k * 0.05;
        const auto s = StrikeAt(t);
        const float flash = SkyWeatherOf(storm, t).flash;
        if (flash > 0) CHECK(s && Near(flash, s->strength * FlashGlow(s->age)));
        if (s) {
            ++strikes;
            CHECK(s->age >= 0 && s->age < kBoltLife && s->strength >= 0.5f && s->strength <= 1.f);
            // 同じ時間なら同じ稲妻。少し後でも、消えるまでは同じ稲妻 (年だけ進む)。
            const auto again = StrikeAt(t);
            CHECK(again && again->seed == s->seed && again->age == s->age);
            if (s->age + 0.02f < kBoltLife) {
                const auto later = StrikeAt(t + 0.02);
                CHECK(later && later->seed >= s->seed);
            }
        }
    }
    CHECK(strikes > 20 && strikes < 3000);
    CHECK(!StrikeAt(0) && !StrikeAt(-1));
    // 明るさ: 落ちた瞬間が一番明るく、一度暗くなり、もう一度光ってから残光で消える。
    CHECK(BoltGlow(0) == 1 && BoltGlow(0.07f) < BoltGlow(0.12f) && BoltGlow(0.12f) > BoltGlow(0.3f) && BoltGlow(0.3f) > BoltGlow(0.6f));
    CHECK(BoltGlow(0.6f) > 0 && BoltGlow(kBoltLife) == 0 && BoltGlow(-0.1f) == 0);
    CHECK(FlashGlow(0) == 1 && FlashGlow(0.45f) == 0 && FlashGlow(0.3f) < BoltGlow(0.3f) + 0.3f);
    // 場所: 地面に、カメラから kBoltNear .. kBoltFar。上の端は雲の下 (地面から kBoltHeight)。
    for (uint32_t seed = 1; seed < 200; ++seed) {
        Vec3 top, bottom;
        BoltPlace(seed, {10, 2, -5}, 0, top, bottom);
        const float d = std::sqrt((bottom.x - 10) * (bottom.x - 10) + (bottom.z + 5) * (bottom.z + 5));
        CHECK(d >= kBoltNear - 0.01f && d <= kBoltFar + 0.01f && bottom.y == 0 && Near(top.y, kBoltHeight));
    }
    // 形: 幹は top から bottom へつながる (128 本)。ぶれは長さの 3 割より小さい。枝は 2 〜 4 本で、細く暗い。
    const Vec3 top{0, 260, 0}, bottom{30, 0, 200};
    const std::vector<BoltSegment> path = BoltPath(7, top, bottom);
    CHECK(path.size() > 128);
    bool joined = Near(path[0].a.x, top.x) && Near(path[0].a.y, top.y) && Near(path[127].b.x, bottom.x) && Near(path[127].b.z, bottom.z);
    const float len = std::sqrt(30.f * 30.f + 260.f * 260.f + 200.f * 200.f);
    bool hugsLine = true;
    for (size_t i = 0; i < 128; ++i) {
        if (i > 0) joined = joined && path[i].a.x == path[i - 1].b.x && path[i].a.y == path[i - 1].b.y && path[i].a.z == path[i - 1].b.z;
        // 線 top-bottom からの距離
        const Vec3 p = path[i].b;
        const Vec3 d{bottom.x - top.x, bottom.y - top.y, bottom.z - top.z};
        const Vec3 q{p.x - top.x, p.y - top.y, p.z - top.z};
        const Vec3 c{q.y * d.z - q.z * d.y, q.z * d.x - q.x * d.z, q.x * d.y - q.y * d.x};
        hugsLine = hugsLine && std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z) / len < 0.3f * len;
        CHECK(path[i].width == 1 && path[i].glow == 1);
    }
    CHECK(joined && hugsLine);
    const size_t twigs = path.size() - 128;
    CHECK(twigs % 32 == 0 && twigs / 32 >= 2 && twigs / 32 <= 4);
    bool thin = true;
    for (size_t i = 128; i < path.size(); ++i) thin = thin && path[i].width < 1 && path[i].glow < 1 && path[i].glow > 0;
    CHECK(thin);
    // 同じ種なら同じ形、違う種なら違う形。
    const std::vector<BoltSegment> same = BoltPath(7, top, bottom), other = BoltPath(8, top, bottom);
    CHECK(same.size() == path.size() && same[60].b.x == path[60].b.x && other[60].b.x != path[60].b.x);
    CHECK(BoltPath(1, top, top).empty());
    // 帯: 1 本につき 12 個の頂点 (芯とにじみ)。カメラの方を向く (帯の横の向きは見る向きと直角)。
    std::vector<BoltVertex> verts;
    const Vec3 eye{0, 2, -50};
    BoltRibbons(path, eye, 1.4f, 10.f, verts);
    CHECK(verts.size() == path.size() * 12);
    const Vec3 side{verts[1].position.x - verts[0].position.x, verts[1].position.y - verts[0].position.y, verts[1].position.z - verts[0].position.z};
    const Vec3 mid{(path[0].a.x + path[0].b.x) / 2 - eye.x, (path[0].a.y + path[0].b.y) / 2 - eye.y, (path[0].a.z + path[0].b.z) / 2 - eye.z};
    const float sideLen = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
    const float midLen = std::sqrt(mid.x * mid.x + mid.y * mid.y + mid.z * mid.z);
    CHECK(Near(sideLen, 1.4f) && std::fabs(side.x * mid.x + side.y * mid.y + side.z * mid.z) / (sideLen * midLen) < 1e-3f);
    CHECK(verts[0].across == -1 && verts[1].across == 1 && verts[6].light < verts[0].light);
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
    const float wet = 0.5f * RainWetSpeed(0.5f);
    CHECK(s.wetCounted && s.wetWays.size() == 1 && Near(s.wetWays[0].sun, wet));  // (Play は乾いた所から始まる: 雨が降るほど濡れる - 0.5 秒)
    s.precipitation = 0;
    RunWeather(ctx);
    CHECK(s.wetWays.size() == 1 && s.wetWays[0].sun < wet && s.wetWays[0].sun > wet - 0.01f);  // ゆっくり乾く
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
    WeatherNow both = snowy;
    both.rain = 1;
    std::vector<WetWay> mixed;
    StepWet(mixed, both, 1000);
    CHECK(mixed.size() == 1 && Near(mixed[0].sun, 1.f) && Near(mixed[0].seep, kSeepRain));  // (同じ向き: 強いほうに合わせる)
    WeatherNow fair;
    StepWet(s, fair, 9);
    CHECK(Near(s[0].sun, kSnowWet - 0.1f));  // (雨のときと同じように乾く)
}

// 積雪: 雪が降る間は深くなり、積もる高さ まで。やむと溶ける (雨では速い)。エディターは積もった状態を見せる。
RM_TEST(Weather_SnowCover) {
    CHECK(Near(WeatherNow{}.snowSpeed, 0.2f / 60.f) && Weather{}.snowSpeedCm == 20.f);  // (既定: 量 1 で 1 分に 20 cm)
    Weather fast;
    fast.snowSpeedCm = 60;
    fast.precipitation = 50;
    fast.snowing = true;
    CHECK(Near(SnowFallRate(NowOf(fast)), 0.3f / 60.f));  // (降る量に比例: 50 で半分)
    fast.snowSpeedCm = std::nanf("");
    CHECK(SnowFallRate(NowOf(fast)) == 0.f);
    WeatherNow snowy;
    snowy.snow = 1;
    snowy.snowSpeed = 0.025f / 60.f;  // (下は 1 分に 2.5 cm として確かめる)
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
    // 絵が見せるとおり: エディター (Play の前と止めた後) は積もっていない、Play は数えたとおり。
    Weather w;
    w.snowing = true;
    w.precipitation = 50;
    w.windStrength = 0;
    CHECK(w.snowDepthCm == 10.f);  // (最初は 10 cm: 足跡が分かる深さ)
    w.snowDepthCm = 34;  // (cm)
    CHECK(Near(SnowMostOf(w), 0.34f) && SnowCoverNow(w) == 0.f);
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
    // 濡れる速さ: どしゃぶりは前と同じ (1 秒に 0.04 = 25 秒)、弱い雨は前 (0.004 + 量 x 0.036) より少し速い。
    CHECK(Near(RainWetSpeed(1), 0.04f) && RainWetSpeed(0) > 0.004f && RainWetSpeed(0) < 0.008f);
    for (const float r : {0.05f, 0.1f, 0.25f, 0.5f, 0.75f}) CHECK(RainWetSpeed(r) > 0.004f + r * 0.036f && RainWetSpeed(r) < 0.04f);
    const float s1 = RainWetSpeed(0.25f);
    std::vector<WetWay> ways;
    StepWet(ways, rain, 1);
    CHECK(ways.size() == 1 && Near(ways[0].sun, s1) && Near(ways[0].shade, s1) && Near(ways[0].way, {0, -1, 0}));
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
    CHECK(Near(fast[0].sun, 0.04f) && fast[0].sun > s1 * 2);  // (強い雨ほど早い: 1 秒に 0.04)
    // 風が回る: 別の方向になる。最初の方向は、雨の間は濡れたままになる。
    WeatherNow windy = rain;
    windy.windX = 10;
    StepWet(ways, windy, 2);
    CHECK(ways.size() == 2 && Near(ways[0].sun, 1.f) && Near(ways[1].sun, 2 * s1) && ways[1].way.x > 0.3f);
    // 少しの風 (kWetWayDegrees 以内): 同じ方向。
    WeatherNow breeze = rain;
    breeze.windX = 0.5f;
    StepWet(ways, breeze, 1);
    CHECK(ways.size() == 2);
    // 雨が弱まる: 降っている間は何も乾かない。
    WeatherNow drizzle = rain;
    drizzle.rain = 0.05f;
    StepWet(ways, drizzle, 50);
    CHECK(Near(ways[0].sun, 1.f) && Near(ways[1].sun, 2 * s1));
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
    CHECK(Near(WetnessOf(ways), 2 * s1) && WetnessOf({}) == 0.f);  // (24 と 30 m/s: kWetWayDegrees 以内 - 方向は 1 つで、濡れは 2 倍)
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
    CHECK(d.size() == 4 * (2 + 2 * kWetWays) && d[0] == 2.f);
    CHECK(d[4] == 0.5f && d[5] == 0.75f && d[6] == 3.f && d[7] == 0.6f && d[8] == 1.f && d[10] == -1.f && d[11] == 0.f);  // (clamped to 1; w: しみこみ)
    CHECK(d.size() <= 4 * space3d::kSurfaceData);
    CHECK(d[20] == 0.f && d[39] == 0.f);  // (方向ごとの雪: なし。場所ごとの地図: なし)
    const WetWay snowy[1] = {{{0, -1, 0}, 0, 0, 0, 0.25f}};
    CHECK(WetSurfaceData(snowy, maps)[20] == 0.25f);
    const std::vector<float> placed = WetSurfaceData(snowy, maps, {-64, 2, 2, 64});  // (surfaceData[9]: 地図の角・1 マス・マス数)
    CHECK(placed[36] == -64.f && placed[37] == 2.f && placed[38] == 2.f && placed[39] == 64.f);
    CHECK(SnowLayerOf({0, 0, 0}, snowy, maps, 6, 1, false, {-64, 2, 2, 64}).place[3] == 64.f && SnowLayerOf({0, 0, 0}, snowy, maps, 6, 1, false).place[3] == 0.f);
}

namespace {

// 地図の左半分 (x が小さい 32 列) が雨、右半分が雪の降り方 (降る量はそのまま)。
std::vector<WeatherPlace> HalfAndHalf() {
    std::vector<WeatherPlace> out(size_t{kWetMapCells} * kWetMapCells);
    for (size_t i = 0; i < out.size(); ++i) out[i] = {i % kWetMapCells < kWetMapCells / 2 ? 0.f : 1.f, 1.f};
    return out;
}

// 天候の口の代わり: x が 0 より小さい所は雨、それ以外は雪。最後に聞かれた真ん中も覚える。
Vec3 gAskedCenter{};
void FakeGrid(const game_production::Scene&, Vec3 center, float step, int count, WeatherPlace* out) {
    gAskedCenter = center;
    const float half = static_cast<float>(count - 1) * 0.5f;
    for (int z = 0; z < count; ++z) {
        for (int x = 0; x < count; ++x) out[z * count + x] = {center.x + (static_cast<float>(x) - half) * step < 0 ? 0.f : 1.f, 1.f};
    }
}

}  // namespace

// 場所ごとの濡れと積雪 (段階 2): カメラのまわりのマス目の地図。雪の所だけ積もり、雨の所だけ濡れ、
// 雨の所の雪は溶ける。どこでも同じ降り方なら、シーン全体で 1 つのとき (StepWet) とまったく同じ。
RM_TEST(Weather_WetMap) {
    int fx = 0, fz = 0;
    WetMapFirst({1.f, 5.f, -0.5f}, fx, fz);
    CHECK(fx == -kWetMapCells / 2 && fz == -1 - kWetMapCells / 2);
    WetMapFirst({std::nanf(""), 0, 1e30f}, fx, fz);  // (おかしな数でも落ちない)
    CHECK(fx == -kWetMapCells / 2);
    // 地図が無いとき: 全部のマスが、シーン全体の濡れの値で始まる。
    WetMap map;
    CHECK(WetCellAt(map, 0, 0) == nullptr && WetMapInfoOf(map).cells == 0.f && WetMapTexels(map).empty());
    std::vector<WetWay> ways = {{{0, -1, 0}, 0.5f, 0.6f, 0, 0.02f}};
    MoveWetMap(map, ways, -32, -32);
    CHECK(map.cells.size() == size_t{kWetMapCells} * kWetMapCells && map.firstX == -32);
    CHECK(Near(WetCellAt(map, 10, -20)->at(0).sun, 0.5f) && Near(WetCellAt(map, 10, -20)->at(0).snow, 0.02f) && WetCellAt(map, 10, -20)->at(1).sun == 0.f);
    const WetMapInfo info = WetMapInfoOf(map);
    CHECK(info.x == -64.f && info.z == -64.f && info.cell == kWetMapCell && info.cells == static_cast<float>(kWetMapCells));
    // 雨と雪 (半分ずつ)、風なし: 向きは 1 つ。1 分で、雨の所は濡れ、雪の所は積もる (と、溶けて少し濡れる)。
    map = {};
    ways.clear();
    MoveWetMap(map, ways, -32, -32);
    const std::vector<WeatherPlace> half = HalfAndHalf();
    WeatherNow now;
    now.snowSpeed = 0.025f / 60.f;  // (1 分に 2.5 cm として確かめる)
    StepWetMap(map, ways, half, now, 0.5f, 60, 0.24f, {-10, 0, 0});
    CHECK(ways.size() == 1 && Near(ways[0].way, {0, -1, 0}) && Near(ways[0].seep, kSeepSnow));  // (しみこみ: どこかで降る雨 (0.36 m) と雪の強いほう)
    const WetCell& rainy = *WetCellAt(map, -10, 0);
    const WetCell& snowy = *WetCellAt(map, 10, 0);
    CHECK(Near(rainy[0].sun, std::min(1.f, 60 * RainWetSpeed(0.5f))) && rainy[0].snow == 0.f);
    CHECK(Near(snowy[0].sun, kSnowWet) && Near(snowy[0].snow, 0.0125f));
    CHECK(Near(ways[0].sun, rainy[0].sun) && ways[0].snow == 0.f);  // (スクリプトが読むのはカメラのいるマス: 雨の所)
    CHECK(Near(WetMapMostSnow(map), 0.0125f));
    // 絵: 左から日なた・日陰・積雪のブロック。1 テクセルに向き 4 つ。
    const std::vector<float> texels = WetMapTexels(map);
    const size_t n = kWetMapCells;
    CHECK(texels.size() == n * 3 * n * 4);
    CHECK(Near(texels[(32 * 3 * n + 40) * 4], snowy[0].sun) && Near(texels[(32 * 3 * n + 2 * n + 40) * 4], 0.0125f) && texels[(32 * 3 * n + 2 * n + 10) * 4] == 0.f);
    // 全部が雨になる: 雪の所の雪は溶ける (雨では速い)、そこも濡れる。
    std::vector<WeatherPlace> allRain(half.size(), WeatherPlace{0, 1});
    StepWetMap(map, ways, allRain, now, 0.5f, 60, 0.24f, {10, 0, 0});
    CHECK(WetCellAt(map, 10, 0)->at(0).snow == 0.f && WetCellAt(map, 10, 0)->at(0).sun > kSnowWet);
    // やむ: 全部のマスで乾いたら向きが消える。
    StepWetMap(map, ways, allRain, now, 0.f, 100000, 0.24f, {10, 0, 0});
    CHECK(ways.empty() && WetCellAt(map, -10, 0)->at(0).sun == 0.f);
    // 一時停止 (dt 0): 何も変わらない。
    StepWetMap(map, ways, half, now, 0.5f, 10, 0.24f, {0, 0, 0});
    const float before = WetCellAt(map, 10, 0)->at(0).snow;
    StepWetMap(map, ways, half, now, 0.5f, 0, 0.24f, {0, 0, 0});
    CHECK(before > 0.f && WetCellAt(map, 10, 0)->at(0).snow == before);
    // カメラが動く: 地図がずれ、新しく入るマスは一番近いマスの値。地図の外は一番近いマス。
    MoveWetMap(map, ways, -29, -32);
    CHECK(map.firstX == -29 && Near(WetCellAt(map, -58 + 1, 0)->at(0).snow, 0.f) && Near(WetCellAt(map, 1000, 0)->at(0).snow, before));
    CHECK(WetCellAt(map, -1000, 0) == &map.cells[size_t{kWetMapCells / 2} * kWetMapCells]);
    // どこでも同じ降り方なら、シーン全体で 1 つのとき (StepWet) と同じ: 風が回り、雪と雨が入れ替わっても。
    WetMap same;
    std::vector<WetWay> mapWays, sceneWays;
    MoveWetMap(same, mapWays, 0, 0);
    struct Phase {
        float wind, snow, precipitation, seconds;
    };
    for (const Phase& p : {Phase{0, 0, 0.6f, 30}, Phase{6, 0, 0.6f, 20}, Phase{12, 1, 0.8f, 200}, Phase{18, 0.5f, 0.3f, 40}, Phase{24, 0, 0.5f, 10},
                           Phase{3, 0, 0, 120}}) {
        WeatherNow w;
        w.windX = p.wind;
        const WeatherPlace everywhere{p.snow, 1};
        PlacedShare(p.precipitation, everywhere, w.rain, w.snow);
        const std::vector<WeatherPlace> places(size_t{kWetMapCells} * kWetMapCells, everywhere);
        StepWetMap(same, mapWays, places, w, p.precipitation, p.seconds, 0.24f, {0, 0, 0});
        StepWet(sceneWays, w, p.seconds, 0.24f);
    }
    CHECK(mapWays.size() == sceneWays.size() && !sceneWays.empty());
    for (size_t k = 0; k < sceneWays.size() && k < mapWays.size(); ++k) {
        const WetLevel& at = WetCellAt(same, 50, 50)->at(k);
        CHECK(Near(mapWays[k].way, sceneWays[k].way) && Near(mapWays[k].seep, sceneWays[k].seep));
        CHECK(Near(at.sun, sceneWays[k].sun) && Near(at.shade, sceneWays[k].shade) && Near(at.snow, sceneWays[k].snow));
    }
    // 降り方の数が合わない: 何もしない。
    StepWetMap(same, mapWays, std::span<const WeatherPlace>(half.data(), 10), WeatherNow{}, 1, 10, 0.24f, {});
    CHECK(Near(WetCellAt(same, 50, 50)->at(0).sun, sceneWays[0].sun));
}

// システム 天候: 答える者がマスを書いたフレームは地図で進め (書いたものは使ったら空にする)、書かない
// フレームは地図を捨ててシーン全体で 1 つに戻る。Play の前 (数えていない) は地図も見せない。
RM_TEST(Weather_WetMapRun) {
    game_production::Scene scene;
    scene.SetSettingTypes({&reflect::TypeOf<Weather>()});
    Weather& w = *scene.Setting<Weather>();
    w.precipitation = 50;
    w.windStrength = 0;
    w.placeSnow = 0.5f;
    w.snowFromStart = false;  // (降るにつれて積もる。はじめから積もっている: Weather_SnowFromStart)
    const WeatherPlaceInfo info{&FakeGrid};
    PlaceCellsFor(w, scene, {3.f, 7.f, 1.f}, info);
    CHECK(w.placeCells.size() == size_t{kWetMapCells} * kWetMapCells && w.placeFirstX == 1 - kWetMapCells / 2 && w.placeFirstZ == -kWetMapCells / 2);
    CHECK(Near(gAskedCenter, {2.f, 7.f, 0.f}));  // (地図の真ん中: カメラのいるマスの角。高さはカメラ)
    CHECK(w.placeCells[0].snow == 0.f && w.placeCells[kWetMapCells - 1].snow == 1.f);
    CHECK(WetMapNow(w) == nullptr);  // (Play の前)
    game_production::SystemContext ctx{scene, 30.f};
    RunWeather(ctx);
    CHECK(w.placeCells.empty() && WetMapNow(w) == &w.wetMap);
    CHECK(WetCellAt(w.wetMap, -20, 0)->at(0).snow == 0.f && WetCellAt(w.wetMap, 20, 0)->at(0).snow > 0.f);  // (雪の所だけ積もる)
    CHECK(WetCellAt(w.wetMap, -20, 0)->at(0).sun > WetCellAt(w.wetMap, 20, 0)->at(0).sun);                 // (雨の所がよく濡れる)
    // 一時停止 (timeScale 0: dt 0) では止まる。
    PlaceCellsFor(w, scene, {3.f, 7.f, 1.f}, info);
    const float snow = WetCellAt(w.wetMap, 20, 0)->at(0).snow;
    game_production::SystemContext paused{scene, 0.f};
    RunWeather(paused);
    CHECK(WetCellAt(w.wetMap, 20, 0)->at(0).snow == snow);
    // 答える者がいなくなった: 地図を捨て、シーン全体で 1 つ。
    RunWeather(ctx);
    CHECK(w.wetMap.cells.empty() && WetMapNow(w) == nullptr && !w.wetWays.empty());
    // Play を止めた (数えていない): 次の Play は乾いた所から。
    PlaceCellsFor(w, scene, {3.f, 7.f, 1.f}, info);
    w.wetCounted = false;
    game_production::SystemContext first{scene, 0.f};
    RunWeather(first);
    CHECK(WetMapNow(w) != nullptr && WetMapMostSnow(w.wetMap) == 0.f && WetCellAt(w.wetMap, -20, 0)->at(0).sun == 0.f);
}

// はじめから積もっている (既定): Play の最初のフレームで、雪の降る所に積もる高さいっぱいの雪。
// シーン全体でも、場所ごとでも (雨の所には置かない)。雪が降っていなければ置かない。
RM_TEST(Weather_SnowFromStart) {
    CHECK(Weather{}.snowFromStart);
    // シーン全体: 雪にする で降っている。
    {
        game_production::Scene scene;
        scene.SetSettingTypes({&reflect::TypeOf<Weather>()});
        Weather& w = *scene.Setting<Weather>();
        w.precipitation = 50;
        w.snowing = true;
        w.windStrength = 0;
        w.snowDepthCm = 20;
        game_production::SystemContext first{scene, 0.f};
        RunWeather(first);
        CHECK(w.wetWays.size() == 1 && Near(SnowCoverNow(w), 0.2f));
        game_production::SystemContext later{scene, 60.f};
        RunWeather(later);
        CHECK(Near(SnowCoverNow(w), 0.2f));  // (積もる高さを超えない)
        // 雨のときは置かない。
        w.wetCounted = false;
        w.snowing = false;
        RunWeather(first);
        CHECK(SnowCoverNow(w) == 0.f);
    }
    // 場所ごと: 雪の所 (x >= 0) だけ。
    {
        game_production::Scene scene;
        scene.SetSettingTypes({&reflect::TypeOf<Weather>()});
        Weather& w = *scene.Setting<Weather>();
        w.precipitation = 50;
        w.windStrength = 0;
        w.placeSnow = 0.5f;
        w.snowDepthCm = 30;
        PlaceCellsFor(w, scene, {3.f, 7.f, 1.f}, WeatherPlaceInfo{&FakeGrid});
        game_production::SystemContext first{scene, 0.f};
        RunWeather(first);
        CHECK(WetMapNow(w) != nullptr && Near(WetCellAt(w.wetMap, 20, 0)->at(0).snow, 0.3f) && WetCellAt(w.wetMap, -20, 0)->at(0).snow == 0.f);
        CHECK(!w.snowStartPending);
    }
    // 純粋な部分: 雪が降っていなければ何もしない。向きがいっぱいなら置かない。
    std::vector<WetWay> ways;
    WeatherNow fair;
    StartSnow(ways, fair, 0.1f);
    CHECK(ways.empty());
    WeatherNow snowy;
    snowy.snow = 0.5f;
    StartSnow(ways, snowy, 0.1f);
    CHECK(ways.size() == 1 && Near(ways[0].snow, 0.1f));
    StartSnow(ways, snowy, 0.f);
    CHECK(ways.size() == 1 && Near(ways[0].snow, 0.1f));  // (高さ 0: 何もしない)
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
    for (const char* entry : {"MeshVS", "ShadowVS", "SkinnedMeshVS", "SkinnedShadowVS", "ClearVS", "LineVS"}) CHECK(Compiles(plain, entry, "vs_4_0", &error));
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
