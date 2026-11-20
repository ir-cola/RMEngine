#pragma once
// 天候 - the scene's weather (a scene setting: the インスペクター shows it under the scene, the 階層's top
// row), all in amounts (2026-11-20: before, 晴れ / くもり / 雨 / 雪 was picked): 雲の量 (few: fair, many:
// cloudy), 雨の量 (the clouds darken into rain clouds; from a few drops to a storm one sees a few metres
// in), 雪を降らせる (snow and a snowy sky come in slowly) and 雪の量, 雷, the wind. The テンプレート
// (快晴 / 雨 / 嵐 / 大雪 ...: WeatherPreset) set them all at once. What it does:
//
//   the sky        地球の空's clouds, their darkness, haze, how much sunlight gets through, lightning, the
//                  clouds drifting with the wind (ゲーム制作 × 3D's SkyWeather: this plugin tells it)
//   rain / snow    drops and flakes falling around the camera, slanted by the wind - more, longer and
//                  heavier drops the harder it rains, and a fog in a downpour; they hit the colliders
//                  (WeatherBlocks.h): rain splashes, a flake stops and melts a moment later, past one
//                  nothing falls (under a roof, unless the wind blows it in) - WeatherDraw.h
//   濡れ / 積雪    what the rain reached looks wet, snow lies where it fell (with footprints) - WeatherDraw.h
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

// The ready-made weathers (the inspector's テンプレート, the scripts' rm::Weather): each sets 雲の量, 雨の量,
// 雪を降らせる / 雪の量, 雷 and 風の強さ at once (ApplyPreset; the wind's way stays).
RM_ENUM(Label="天気のテンプレート")
enum class WeatherPreset {
    RM_ITEM(Label="快晴") Clear,
    RM_ITEM(Label="晴れ") Fair,
    RM_ITEM(Label="くもり") Cloudy,
    RM_ITEM(Label="ぽつぽつ雨") Sprinkle,
    RM_ITEM(Label="小雨") Drizzle,
    RM_ITEM(Label="雨") Rain,
    RM_ITEM(Label="どしゃぶり") Downpour,
    RM_ITEM(Label="嵐") Storm,
    RM_ITEM(Label="小雪") LightSnow,
    RM_ITEM(Label="雪") Snow,
    RM_ITEM(Label="大雪") HeavySnow,
    RM_ITEM(Label="吹雪") Blizzard,
};
inline constexpr int kWeatherPresets = 12;

// The weather as it is at a moment (amounts 0 .. 1: the fields' / 100). Not saved (Weather keeps what
// it was when a fade began: fadeFrom).
struct WeatherNow {
    float clouds = 0.2f;  // 雲の量
    float rain = 0;       // 雨の量: what falls as rain
    float snow = 0;       // 雪の量 as far as 雪を降らせる has come in: what falls as snow
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
    // 積雪: how deep the snow that fell this way lies (m) where it reached - on what the way reaches
    // (slanted by the wind: under a roof's edge too). Where several ways reach, their snow adds up.
    float snow = 0;
};
inline constexpr size_t kWetWays = 4;  // the most kept (a 向きの地図 each)
inline constexpr float kSeepRain = 1.f, kSeepSnow = 0.4f;

RM_STRUCT(Label="天候")
struct Weather {
    RM_PROPERTY(Edit, Label="雲の量", Tooltip="0: 雲ひとつ無い晴れ 〜 40: 雲の多い晴れ 〜 70: くもり 〜 100: 空一面の厚い雲 (暗くなり、日の光も弱まります)。雨や雪が降ると、少なくても雲が出ます (雲は空が「地球の空」のとき)", Min=0, Max=100, Step=0.1, Decimals=1) float cloudAmount = 20.f;
    RM_PROPERTY(Edit, Label="雨の量", Tooltip="0: 降らない / 〜10: ぽつぽつ / 〜30: 小雨 / 〜60: 雨 / 〜85: どしゃぶり / 100: 数 m 先しか見えない嵐。増えるほど雲が暗い雨雲になり、雨粒が長く重くなります", Min=0, Max=100, Step=0.1, Decimals=1) float rainAmount = 0.f;
    RM_PROPERTY(Edit, Label="雪を降らせる", Tooltip="入れると、10 秒ほどかけて雪と雪空に変わります。外すと同じくらいかけてやみます (エディターでは、すぐその見た目)") bool snowing = false;
    RM_PROPERTY(Edit, Label="雪の量", Tooltip="0: 降らない / 〜20: ちらつく / 〜50: 雪 / 〜80: 大雪 / 100: 吹雪 (先が見えにくい)", Min=0, Max=100, Step=0.1, Decimals=1, ShowIf="snowing") float snowAmount = 50.f;
    RM_PROPERTY(Edit, Label="積もる高さ (m)", Tooltip="雪がこの高さまで積もります (Play 中は降っている間に少しずつ。大雪で 4 分ほど)。0: 積もらない。屋根の下には積もらず、やむと少しずつ溶けます (雨だと早く)", Min=0, Max=2, Step=0.01, ShowIf="snowing") float snowDepth = 0.1f;
    RM_PROPERTY(Edit, Label="雷", Tooltip="ときどき雲が光り、そのとき場面も一瞬明るくなります (Play 中。雲の多いとき)") bool lightning = false;
    RM_PROPERTY(Edit, Label="風の向き (度)", Tooltip="風の吹いていく向き (真上から見て)。0: 前 (-Z) へ / 90: 左 (-X) へ / -90: 右 (+X) へ。雲・雨粒・雪が流されます", Min=-360, Max=360) float windDirection = 0.f;
    RM_PROPERTY(Edit, Label="風の強さ (m/秒)", Tooltip="0: 無風 〜 30: 嵐。雲の流れる速さと、雨粒・雪の傾き", Min=0, Max=40, Step=0.5) float windStrength = 2.f;
    // 雪を降らせる coming in (0 .. 1) while the game plays: towards it, over about 10 seconds (the editor
    // before Play: as the box says - wetCounted).
    RM_TRANSIENT() float snowIn = 0;
    // A fade (scripts: rm::Sky's SetWeather / SetRain ... with seconds): the weather when it began, how
    // long it takes, how far it has come. The fields above are where it goes.
    RM_TRANSIENT() WeatherNow fadeFrom{};
    RM_TRANSIENT() float fadeSeconds = 0;
    RM_TRANSIENT() float fadeElapsed = 0;
    // 濡れ while the game plays (StepWet): the ways the rain fell and how wet each made things. Not
    // counted yet (the editor before Play): what the rain falling now makes wet, at once (WetAtOnce).
    RM_TRANSIENT() std::vector<WetWay> wetWays;
    RM_TRANSIENT() bool wetCounted = false;
    // How far the wind has carried the clouds, the drops and the flakes while the game played (world
    // units: the wind added up - a wind that changes never makes them jump).
    RM_TRANSIENT() float driftX = 0;
    RM_TRANSIENT() float driftZ = 0;
};

// Sets the fields to a ready-made weather (WeatherPreset): 雲の量, 雨の量, 雪を降らせる / 雪の量, 雷 and 風の
// 強さ (風の向き stays). A value of a newer version: nothing.
void ApplyPreset(Weather& w, WeatherPreset preset);
// How far 雪を降らせる has come in (0 .. 1): as the game counted it, or - the editor - as the box says.
float SnowInOf(const Weather& w);
// The weather the fields say (where a fade goes) / the weather now (a fade's way there: eased).
WeatherNow TargetOf(const Weather& w);
WeatherNow NowOf(const Weather& w);
// Starts a fade of `seconds` from the weather now: change the fields after it (0 seconds: at once).
// A fade begun during another starts from where that one has come.
void BeginFade(Weather& w, float seconds);
// How much rain / snow falls now (0 .. 1).
float RainNow(const WeatherNow& w);
float SnowNow(const WeatherNow& w);
// How thick the fog of a heavy downpour / snowstorm is (per metre, 0: none): a few metres seen at 100.
float FogOf(const WeatherNow& w);
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
// 積雪 (`snowMost`: 積もる高さ): the snow lying, all the ways together, goes as StepSnowCover says - what
// falls goes to the way it falls (WetWay::snow), what melts goes from all of them alike.
void StepWet(std::vector<WetWay>& ways, const WeatherNow& now, float dt, float snowMost = 0);
// What is wet now, as the editor shows it before Play: what falls now (rain, snow), its way, as wet as it
// makes things. (Play
// starts dry and gets wet as it rains: RunWeather.)
std::vector<WetWay> WetAtOnce(const WeatherNow& now, float snowMost = 0);
// How wet it is (the scripts' rm::Sky::Wetness): the wettest way in the sun.
float WetnessOf(const std::vector<WetWay>& ways);
// What is wet as a picture shows it: as the game has counted it (Play), or at once.
std::vector<WetWay> WetNow(const Weather& w);

// 積雪: how deep the snow lies `dt` game seconds later (m), all of it. While it snows: deeper, up to `most`
// (積もる高さ) - in about four minutes in heavy snow, longer the lighter (never above `most`: lowered, it
// is at once). Nothing snowing: it melts - about a centimetre a minute, faster in rain. (StepWet shares
// it out among the ways it fell.)
inline constexpr float kMostSnowDepth = 2.f;
float StepSnowCover(float cover, const WeatherNow& now, float most, float dt);
// How fast the snow lying grows now (m a second; 0: not snowing) - footprints fill up as fast.
float SnowFallRate(const WeatherNow& now, float most);
// How deep the snow lies as a picture shows it, all the ways together: as the game has counted it (Play),
// or - the editor - at its full 積もる高さ while it snows.
float SnowCoverNow(const Weather& w);

// The system 天候: while the game plays, a fade goes on, 雪を降らせる comes in (or goes), what the rain
// reaches gets wet (and dries), snow lies, and the wind carries the clouds and what falls.
void RunWeather(game_production::SystemContext& ctx);

}  // namespace rm::weather
