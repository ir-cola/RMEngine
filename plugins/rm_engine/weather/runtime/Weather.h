#pragma once
// 天候 - シーンの天候 (シーンの設定: インスペクターのシーンの下、階層の一番上の行に出る)。すべて量で持つ
// (2026-11-20: 前は 晴れ / くもり / 雨 / 雪 を選んでいた): 雲の量 (少ない: 晴れ、多い:
// くもり)、降水量 (雨か雪 - 両方にはならない: 雲が暗い雨雲になる。ぽつぽつから嵐まで)、
// 雪にする (雨が弱まって霧雨になり、雪に変わる - 戻ることもある)、霧の量 (色は天候で決まる)、雷、風。
// テンプレート (快晴 / 霧 / 雨 / 嵐 / 大雪 ...: WeatherPreset) はこれらを一度に決める。
// やること:
//
//   空             地球の空の雲、その暗さ、かすみ、通る日光の量、雷、風で流れる雲
//                  (ゲーム制作 × 3D の SkyWeather: このプラグインが伝える)
//   雨 / 雪        カメラのまわりに降る粒とひらひら、風で斜めになる。強く降るほど粒は
//                  多く、長く、重くなり、どしゃぶりでは霧も出る。コライダー (WeatherBlocks.h)
//                  に当たる: 雨ははねる、雪は止まって少しあとに溶ける、何かの下には
//                  降らない (屋根の下。風が吹き込む場合は除く) - WeatherDraw.h
//   濡れ / 積雪    雨が届いた所は濡れて見え、雪は降った所に積もる (足跡つき) - WeatherDraw.h
//   スクリプト     rm::Sky::SetWeather / SetRain / SetWind ... 即座に、またはなめらかに (RMWeather.h)
//
// 純粋 (ただし RunWeather は除く)。tests/TestWeather.cpp でテストする。

#include "base/Math.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"

#include <cstdint>
#include <vector>

namespace rm::game_production {
struct SystemContext;
}

namespace rm::weather {

// できあいの天候 (インスペクターのテンプレート、スクリプトの rm::Weather): それぞれ、雲の量、降水量、
// 雪にする、霧の量、雷、風の強さを一度に決める (ApplyPreset。風の向きと積もる高さはそのまま)。
RM_ENUM(Label="天気のテンプレート")
enum class WeatherPreset {
    RM_ITEM(Label="快晴") Clear,
    RM_ITEM(Label="晴れ") Fair,
    RM_ITEM(Label="くもり") Cloudy,
    RM_ITEM(Label="霧") Foggy,
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
inline constexpr int kWeatherPresets = 13;

// ある瞬間の天候 (量は 0 .. 1: 各項目を 100 で割ったもの)。保存しない (Weather は
// フェードの始まりの状態を持つ: fadeFrom)。
struct WeatherNow {
    float clouds = 0.2f;  // 雲の量
    float rain = 0;       // 雨として降るもの (降水量のうち、雪にする が許す分)
    float snow = 0;       // 雪として降るもの
    float fog = 0;        // 霧の量
    float windX = 0, windZ = 0;  // 風の行き先、真上から見たもの (m/s、ワールド)
};

// 濡れ: 雨が降っている間にその雨が降った向き (風で斜めになる) と、その向きで雨が届いたものがどれだけ濡れているか
// - 日なたと日かげで (日かげは乾くのが遅い)。どの向きでも雨が届いた所は濡れる。一度届いた所は、
// 雨が降っている間はそのあと風が変わっても濡れたまま。
struct WetWay {
    Vec3 way{0, -1, 0};        // 長さ 1
    float sun = 0, shade = 0;  // 0 .. 1
    // しみこみ: 雨が届いた所から届かなかった所へ、濡れがどれだけ (m) 広がったか (球・ベンチ・樽の下:
    // 水が地面に広がる)。長く降るほど長くなり、kSeepRain まで (雪: kSeepSnow)。乾く間は保つ。
    // これより小さい物の下では全部が濡れる。屋根の下では端だけ。流れる水の代わり: 流体のプラグインが
    // あれば、それが決められる (または地面そのものを濡らす) - 面の手触りが読むのはこの数だけ。
    float seep = 0;
    // 積雪: この向きで降った雪が届いた所にどれだけ積もるか (m)。その向きが届く所に (風で斜めになる:
    // 屋根の端の下にも)。いくつかの向きが届く所では、その雪が足される。
    float snow = 0;
};
inline constexpr size_t kWetWays = 4;  // 持てる最大数 (それぞれ向きの地図 1 枚)
inline constexpr float kSeepRain = 1.f, kSeepSnow = 0.4f;

RM_STRUCT(Label="天候")
struct Weather {
    RM_PROPERTY(Edit, Label="雲の量", Tooltip="0: 雲ひとつ無い晴れ 〜 40: 雲の多い晴れ 〜 70: くもり 〜 100: 空一面の厚い雲 (暗くなり、日の光も弱まります)。雨や雪が降ると、少なくても雲が出ます (雲は空が「地球の空」のとき)", Min=0, Max=100, Step=0.1, Decimals=1) float cloudAmount = 20.f;
    RM_PROPERTY(Edit, Label="降水量", Tooltip="雨 (雪にするときは雪) の降る量。0: 降らない / 〜10: ぽつぽつ / 〜30: 小雨 / 〜60: 雨 / 〜85: どしゃぶり / 100: 嵐 (雪: 〜20 ちらつく / 〜50 雪 / 〜80 大雪 / 100 吹雪)。増えるほど雲が暗くなり、強い雨・雪では霧も出ます", Min=0, Max=100, Step=0.1, Decimals=1) float precipitation = 0.f;
    RM_PROPERTY(Edit, Label="雪にする", Tooltip="入れると、雨がだんだん小雨になってから雪に変わり、雪が強まって降水量に戻ります。外すと逆に、雪が弱まってから雨に (Play 中は 5 秒ほど。エディターではすぐその見た目)。雨と雪が一緒に降ることはありません") bool snowing = false;
    RM_PROPERTY(Edit, Label="積もる高さ (cm)", Tooltip="雪がこの高さまで積もります (cm、整数。最大 1000)。Play 中は降っている間に少しずつ、降る量が多いほど速く (降水量 100 で 1 分に 2.5 cm ほど)。0: 積もらない。屋根の下には積もらず、やむと少しずつ溶けます (雨だと早く)", Min=0, Max=1000, Step=1, Decimals=0, ShowIf="snowing") float snowDepthCm = 10.f;
    RM_PROPERTY(Edit, Label="霧の量", Tooltip="0: 無し 〜 50: 20 m ほど先がかすむ 〜 100: 数 m 先しか見えない。色は天気で変わります (晴れ・くもり: 白っぽい / 雨: 暗い灰色 / 雪: 白)。強い雨・雪では、これに加えて霧が出ます", Min=0, Max=100, Step=0.1, Decimals=1) float fogAmount = 0.f;
    RM_PROPERTY(Edit, Label="雷", Tooltip="ときどき雲が光り、そのとき場面も一瞬明るくなります (Play 中。雲の多いとき)") bool lightning = false;
    RM_PROPERTY(Edit, Label="風の向き (度)", Tooltip="風の吹いていく向き (真上から見て)。0: 前 (+Z) へ / 90: 左 (-X) へ / -90: 右 (+X) へ。雲・雨粒・雪が流されます", Min=-360, Max=360) float windDirection = 0.f;
    RM_PROPERTY(Edit, Label="風の強さ (m/秒)", Tooltip="0: 無風 〜 30: 嵐。雲の流れる速さと、雨粒・雪の傾き", Min=0, Max=40, Step=0.5) float windStrength = 2.f;
    // ゲームが動いている間に 雪にする がどこまで来たか (0: 雨 .. 1: 雪)。約 5 秒かけて向かう
    // (Play 前のエディター: 箱の言うとおり - wetCounted)。
    RM_TRANSIENT() float snowIn = 0;
    // フェード (スクリプト: rm::Sky の SetWeather / SetRain ... に秒を付けたもの): 始まった時の天候、
    // かかる時間、どこまで来たか。上のフィールドが行き先。
    RM_TRANSIENT() WeatherNow fadeFrom{};
    RM_TRANSIENT() float fadeSeconds = 0;
    RM_TRANSIENT() float fadeElapsed = 0;
    // ゲームが動いている間の 濡れ (StepWet): 雨が降った向きと、それぞれがどれだけ濡らしたか。まだ
    // 数えていない (Play 前と止めた後のエディター) ときは、濡れも積雪も無い (2026-11-22: 前は降った後の見た目)。
    RM_TRANSIENT() std::vector<WetWay> wetWays;
    RM_TRANSIENT() bool wetCounted = false;
    // ゲームが動いている間に、風が雲・粒・ひらひらをどれだけ運んだか (ワールド単位: 風を足し
    // 合わせたもの - 風が変わっても跳ばない)。
    RM_TRANSIENT() float driftX = 0;
    RM_TRANSIENT() float driftZ = 0;
};

// フィールドをできあいの天候 (WeatherPreset) にする: 雲の量、降水量、雪にする、霧の量、雷、風の強さ
// (風の向きと積もる高さはそのまま)。新しい版の値: 何もしない。
void ApplyPreset(Weather& w, WeatherPreset preset);
// 雪にする がどこまで来たか (0: 雨 .. 1: 雪): ゲームが数えたとおり、エディターでは箱の言うとおり。
float SnowInOf(const Weather& w);
// 間の道のりで、降水量のうち雨として降る分 / 雪として降る分 (x: SnowInOf): 雨が弱まって霧雨になり
// (真ん中で 0.15)、そこから軽い雪になって増えていく - 両方が同時に降ることはない。
float RainShare(float x);
float SnowShare(float x);
// 積もる高さ in metres.
float SnowMostOf(const Weather& w);
// フィールドの言う天候 (フェードの行き先) / 今の天候 (フェードの途中: 緩急つき)。
WeatherNow TargetOf(const Weather& w);
WeatherNow NowOf(const Weather& w);
// 今の天候から `seconds` 秒のフェードを始める: そのあとフィールドを変える (0 秒: すぐに)。
// フェード中に始めたフェードは、そのフェードが来たところから始まる。
void BeginFade(Weather& w, float seconds);
// 今、雨 / 雪がどれだけ降っているか (0 .. 1)。
float RainNow(const WeatherNow& w);
float SnowNow(const WeatherNow& w);
// 霧の濃さ (1 m あたり、0: なし): 霧の量 (100 で数 m 先まで、50 で 20 m 先まで見える)。どしゃぶり /
// 吹雪ではさらに濃くなる。色 (リニア) は空気を照らすものから決まる: 淡く、雨では暗く灰色に、
// 雪では白く。
float FogOf(const WeatherNow& w);
Color FogColorOf(const WeatherNow& w, Color light);
// 風の向き / 風の強さ の風 (m/s、ワールド: y は 0)。
Vec3 WindOf(float directionDegrees, float strength);

// 天候が `seconds` (ゲームの時間) の空に何をするか (雷は時間が進んでいる間だけ)。
game_x_3d::SkyWeather SkyWeatherOf(const Weather& w, double seconds);

// `dt` ゲーム秒あとの 濡れ。雨の間: 今降っている向き (風で斜め) が濡れていく
// (最後は全部: RainWetSpeed。どしゃぶりで約 25 秒、霧雨で 1 分半) - kWetWayDegrees 以内の向きが
// すでにあれば、それとみなす。ほかの向きは今までと同じ濡れのまま (雨が届いた所は、
// 降っている間は濡れたまま)。雨が降っていない間: すべて乾く - 日なたで 1 分半、日かげで 5 分あまり - そして
// 乾いたら消える。最大 kWetWays (新しい向きは、一番濡れていないものに代わる)。降る向きは、覆いの下への
// しみこみも進める (WetWay::seep: どしゃぶりは 2 分足らずで 1 m、霧雨は 7 分ほど)。
inline constexpr float kWetWayDegrees = 8.f;
// 雪も溶ける所で濡らす - 少しだけ: 多くてもこの値 (雨: 1)、もっとゆっくり。雨や雪が降っている間は
// 何も乾かない。
inline constexpr float kSnowWet = 0.45f;
// 積雪 (`snowMost`: 積もる高さ): 積もっている雪を、すべての向きまとめて、StepSnowCover の言うとおりに
// 動かす - 降ったものは降る向きに入り (WetWay::snow)、溶けるものは全部の向きから同じだけ引く。
void StepWet(std::vector<WetWay>& ways, const WeatherNow& now, float dt, float snowMost = 0);
// 雨 (0 .. 1) が 1 秒にどれだけ濡らすか (最後は 1)。どしゃぶり (1) で 0.04 (25 秒)、弱い雨は量に比べて
// 少し速め (2026-11-22)。
float RainWetSpeed(float rain);
// どれだけ濡れているか (スクリプトの rm::Sky::Wetness): 日なたで一番濡れている向き。
float WetnessOf(const std::vector<WetWay>& ways);
// 絵が見せる濡れ: ゲームが数えたとおり (Play)。Play の前と止めた後は無い (Play の跡は残さない)。
std::vector<WetWay> WetNow(const Weather& w);

// 積雪: `dt` ゲーム秒あとの雪の深さ (m)、全部。雪の間: `most` (積もる高さ) まで、降る雪の
// 量に比べた速さで深くなる (SnowFallRate。`most` を超えない: 下げたときはすぐに)。雪が降って
// いない間は溶ける - 1 分に 1 cm ほど、雨だと速い。(StepWet が、降った向きごとに分ける。)
// 積もる高さは多くて kMostSnowDepth (10 m。2026-11-20: 前は 5 cm まで)。
inline constexpr float kMostSnowDepth = 10.f;
float StepSnowCover(float cover, const WeatherNow& now, float most, float dt);
// 積もっている雪が今どれだけの速さで増えるか (1 秒あたり m、0: 雪ではない) - 降る雪の量だけで
// 決まる (量 1 で 1 分に 2.5 cm。2026-11-20: 前は積もる高さに比べていて、どの高さでも 4 分で
// 積もりきった)。足跡も同じ速さで埋まる。
inline constexpr float kSnowFallMost = 0.025f / 60.f;
float SnowFallRate(const WeatherNow& now);
// 絵が見せる雪の深さ、すべての向きまとめて: ゲームが数えたとおり (Play)、またはエディターでは
// 雪が降っている間は積もる高さいっぱいで。
float SnowCoverNow(const Weather& w);

// システム 天候: ゲームが動いている間、フェードが進み、雪を降らせる が入り (または抜け)、
// 雨が届いた所が濡れ (そして乾き)、雪が積もり、風が雲と降るものを運ぶ。
void RunWeather(game_production::SystemContext& ctx);

}  // namespace rm::weather
