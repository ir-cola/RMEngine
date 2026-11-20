#pragma once
// 天候 - the scene's weather (a scene setting: the インスペクター shows it under the scene, the 階層's top
// row): 晴れ / くもり / 雨 / 雪, how cloudy, lightning, how much rain or snow, the wind. What it does:
//
//   the sky        地球の空's clouds, their darkness, haze, how much sunlight gets through, lightning, the
//                  clouds drifting with the wind (ゲーム制作 × 3D's SkyWeather: this plugin tells it)
//   rain / snow    drops and flakes falling around the camera, slanted by the wind; they hit the
//                  colliders (WeatherBlocks.h): rain splashes, a flake stops and melts a moment later,
//                  past one nothing falls (under a roof, unless the wind blows it in) - WeatherDraw.h
//   濡れ           what the rain reached looks wet (round spots, a little darker, smooth); it stays wet
//                  while it rains and dries in game time after (slower in shade) - WeatherDraw.h
//   scripts        rm::Sky::SetWeather / SetRain / SetWind ... at once or faded (RMWeather.h)
//
// Pure (but RunWeather) - tested in tests/TestWeather.cpp.

#include "base/Math.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"

#include <cstdint>
#include <vector>

namespace rm::game_production {
struct SystemContext;
}

namespace rm::weather {

RM_ENUM(Label="天気")
enum class WeatherKind {
    RM_ITEM(Label="晴れ") Clear,
    RM_ITEM(Label="くもり") Cloudy,
    RM_ITEM(Label="雨") Rain,
    RM_ITEM(Label="雪") Snow,
};

// The weather as it is at a moment: how much of each of the four (they add up to 1 - one of them, or a
// mix while one fades into another) and the amounts. Not saved (Weather keeps what it was when a fade
// began: fadeFrom).
struct WeatherNow {
    float weight[4] = {1, 0, 0, 0};  // by WeatherKind
    float clearClouds = 0.2f, cloudiness = 0.7f, rain = 0.5f, snow = 0.5f;
    float windX = 0, windZ = 0;  // where the wind goes, seen from above (m/s, world)
};

// 濡れ: a way the rain fell while it rained (slanted by the wind) and how wet what it reached that way
// is - in the sun and in shade (shade dries slower). What the rain reached any of the ways is wet: a
// place it reached stays wet while it rains, whatever the wind does after.
struct WetWay {
    Vec3 way{0, -1, 0};        // length 1
    float sun = 0, shade = 0;  // 0 .. 1
    // しみこみ: how far (metres) the wet has crept from where the rain reached into what it did not (under
    // a ball, a bench, a barrel: the water spreads over the ground) - longer the longer it falls, up to
    // kSeepRain (snow: kSeepSnow); kept while it dries. Under a thing smaller than this all of it gets wet;
    // under a roof only its edge. A stand-in for water that flows: a fluid plugin, when there is one, can
    // set it (or wet the ground itself) - the touch on the surfaces reads only this number.
    float seep = 0;
};
inline constexpr size_t kWetWays = 4;  // the most kept (a 向きの地図 each)
inline constexpr float kSeepRain = 1.f, kSeepSnow = 0.4f;

RM_STRUCT(Label="天候")
struct Weather {
    RM_PROPERTY(Edit, Label="天気", Tooltip="晴れ: 日の差す空に雲を少し (量を選べる) / くもり: 雲の多い暗い空 / 雨・雪: 厚い雲に加えて、カメラのまわりに雨粒・雪が降ります (雲は空が「地球の空」のとき)") WeatherKind kind = WeatherKind::Clear;
    // (晴れ has its own 雲の量: 0.2 is the few clouds it always had)
    RM_PROPERTY(Edit, Label="雲の量", Tooltip="晴れの空の雲。0: 雲ひとつ無い 〜 1: 雲が多い (日の光はほとんど弱まりません)", Min=0, Max=1, Step=0.05, ShowIf="kind=Clear") float clearClouds = 0.2f;
    RM_PROPERTY(Edit, Label="雲の量", Tooltip="0: 雲が多め 〜 1: 空一面の厚い雲 (暗くなり、日の光も弱まります)", Min=0, Max=1, Step=0.05, ShowIf="kind=Cloudy|Rain|Snow") float cloudiness = 0.7f;
    RM_PROPERTY(Edit, Label="雷", Tooltip="ときどき雲が光り、そのとき場面も一瞬明るくなります (Play 中)", ShowIf="kind=Cloudy|Rain") bool lightning = false;
    RM_PROPERTY(Edit, Label="雨の量", Tooltip="0: 降らない 〜 1: どしゃぶり", Min=0, Max=1, Step=0.05, ShowIf="kind=Rain") float rain = 0.5f;
    RM_PROPERTY(Edit, Label="雪の量", Tooltip="0: 降らない 〜 1: 大雪", Min=0, Max=1, Step=0.05, ShowIf="kind=Snow") float snow = 0.5f;
    RM_PROPERTY(Edit, Label="積もる高さ (m)", Tooltip="雪がこの高さまで積もります (Play 中は降っている間に少しずつ。大雪で 4 分ほど)。0: 積もらない。屋根の下には積もらず、やむと少しずつ溶けます (雨だと早く)", Min=0, Max=2, Step=0.01, ShowIf="kind=Snow") float snowDepth = 0.1f;
    RM_PROPERTY(Edit, Label="風の向き (度)", Tooltip="風の吹いていく向き (真上から見て)。0: 前 (-Z) へ / 90: 左 (-X) へ / -90: 右 (+X) へ。雲・雨粒・雪が流されます", Min=-360, Max=360) float windDirection = 0.f;
    RM_PROPERTY(Edit, Label="風の強さ (m/秒)", Tooltip="0: 無風 〜 30: 嵐。雲の流れる速さと、雨粒・雪の傾き", Min=0, Max=40, Step=0.5) float windStrength = 2.f;
    // A fade (scripts: rm::Sky's SetWeather / SetRain ... with seconds): the weather when it began, how
    // long it takes, how far it has come. The fields above are where it goes.
    RM_TRANSIENT() WeatherNow fadeFrom{};
    RM_TRANSIENT() float fadeSeconds = 0;
    RM_TRANSIENT() float fadeElapsed = 0;
    // 濡れ while the game plays (StepWet): the ways the rain fell and how wet each made things. Not
    // counted yet (the editor before Play): what the rain falling now makes wet, at once (WetAtOnce).
    RM_TRANSIENT() std::vector<WetWay> wetWays;
    RM_TRANSIENT() bool wetCounted = false;
    // 積雪 while the game plays (StepSnowCover): how deep the snow lies (m). Counted with 濡れ (wetCounted).
    RM_TRANSIENT() float snowCover = 0;
    // How far the wind has carried the clouds, the drops and the flakes while the game played (world
    // units: the wind added up - a wind that changes never makes them jump).
    RM_TRANSIENT() float driftX = 0;
    RM_TRANSIENT() float driftZ = 0;
};

// The weather the fields say (where a fade goes) / the weather now (a fade's way there: eased).
WeatherNow TargetOf(const Weather& w);
WeatherNow NowOf(const Weather& w);
// Starts a fade of `seconds` from the weather now: change the fields after it (0 seconds: at once).
// A fade begun during another starts from where that one has come.
void BeginFade(Weather& w, float seconds);
// How much rain / snow falls now (the weather's share x its amount: 0 .. 1).
float RainNow(const WeatherNow& w);
float SnowNow(const WeatherNow& w);
// The wind of 風の向き / 風の強さ (m/s, world: y is 0).
Vec3 WindOf(float directionDegrees, float strength);

// What the weather does to the sky at `seconds` (the game's time: lightning only while it goes on).
game_x_3d::SkyWeather SkyWeatherOf(const Weather& w, double seconds);

// 濡れ `dt` game seconds later. While it rains: the way it falls now (slanted by the wind) gets wetter
// (all of it in the end: in about 25 seconds in a downpour, two minutes in a drizzle) - a way within kWetWayDegrees of one
// kept is that one; the others stay as wet as they were (what the rain reached stays wet while it
// rains). Not raining: all dry - in the sun in a minute and a half, in shade over five minutes - and
// go when dry. At most kWetWays (a new way takes the place of the least wet one). The way it falls also
// seeps further under what covers (WetWay::seep: a downpour 1 m in under two minutes, a drizzle in
// about seven).
inline constexpr float kWetWayDegrees = 8.f;
// Snow wets too, where it melts - a little: at most this (rain: 1), more slowly. Nothing dries while
// rain or snow falls.
inline constexpr float kSnowWet = 0.45f;
void StepWet(std::vector<WetWay>& ways, const WeatherNow& now, float dt);
// What is wet now, as the editor shows it before Play: what falls now (rain, snow), its way, as wet as it
// makes things. (Play
// starts dry and gets wet as it rains: RunWeather.)
std::vector<WetWay> WetAtOnce(const WeatherNow& now);
// How wet it is (the scripts' rm::Sky::Wetness): the wettest way in the sun.
float WetnessOf(const std::vector<WetWay>& ways);
// What is wet as a picture shows it: as the game has counted it (Play), or at once.
std::vector<WetWay> WetNow(const Weather& w);

// 積雪: how deep the snow lies `dt` game seconds later (m). While it snows: deeper, up to `most` (積もる
// 高さ) - in about four minutes in heavy snow, longer the lighter (never above `most`: lowered, it is
// at once). Nothing snowing: it melts - about a centimetre a minute, faster in rain. Lies on what faces
// up and the snow reaches straight down (under a roof: none).
inline constexpr float kMostSnowDepth = 2.f;
float StepSnowCover(float cover, const WeatherNow& now, float most, float dt);
// How deep the snow lies as a picture shows it: as the game has counted it (Play), or - the editor - at
// its full 積もる高さ while it snows.
float SnowCoverNow(const Weather& w);

// The system 天候: while the game plays, a fade goes on, what the rain reaches gets wet (and dries),
// and the wind carries the clouds and what falls.
void RunWeather(game_production::SystemContext& ctx);

}  // namespace rm::weather
