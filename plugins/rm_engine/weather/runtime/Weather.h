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
#include "rm_engine/weather/runtime/WeatherPlace.h"

#include <array>
#include <cstdint>
#include <span>
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
    float snowSpeed = 0.2f / 60.f;  // 雪の量 1 のとき積もる速さ (m / 秒。Weather::snowSpeedCm から)
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

// 場所ごとの濡れと積雪 (段階 2。docs/CLIMATE.md): 1 つの場所の、向き k (WetWay と同じ並び) ごとの
// 日なた・日陰の濡れと積雪 (m)。
struct WetLevel {
    float sun = 0, shade = 0, snow = 0;
};
using WetCell = std::array<WetLevel, kWetWays>;
// 地面を真上から見たマス目の地図: カメラのまわり kWetMapCells x kWetMapCells マス (1 マス kWetMapCell m:
// 128 m 四方)。マスはワールドに固定 (カメラが動くと地図がずれる。新しく入るマスは、地図の一番近い
// マスの値で始まる。地図の外も、描くときは一番近いマス)。
inline constexpr int kWetMapCells = 64;
inline constexpr float kWetMapCell = 2.f;
// 場所ごとの雲の地図 (気候の地図に塗った雲。docs/CLIMATE.md の「気候ビュー」): カメラのまわり
// kCloudMapCells x kCloudMapCells マス (1 マス kCloudMapCell m: 3 km 四方ほど)。空は遠くまで見えるので粗い。
inline constexpr int kCloudMapCells = 48;
inline constexpr float kCloudMapCell = 64.f;
struct WetMap {
    int firstX = 0, firstZ = 0;  // 最初のマス (ワールドのマス番号: floor(x / kWetMapCell))
    std::vector<WetCell> cells;  // kWetMapCells^2 (行が z、z の小さい行から)。空: 地図を使わない
};

RM_STRUCT(Label="天候")
struct Weather {
    RM_PROPERTY(Edit, Label="雲の量", Tooltip="0: 雲ひとつ無い晴れ 〜 40: 雲の多い晴れ 〜 70: くもり 〜 100: 空一面の厚い雲 (暗くなり、日の光も弱まります)。雨や雪が降ると、少なくても雲が出ます (雲は空が「地球の空」のとき)", Min=0, Max=100, Step=0.1, Decimals=1) float cloudAmount = 20.f;
    RM_PROPERTY(Edit, Label="降水量", Tooltip="雨 (雪にするときは雪) の降る量。0: 降らない / 〜10: ぽつぽつ / 〜30: 小雨 / 〜60: 雨 / 〜85: どしゃぶり / 100: 嵐 (雪: 〜20 ちらつく / 〜50 雪 / 〜80 大雪 / 100 吹雪)。増えるほど雲が暗くなり、強い雨・雪では霧も出ます", Min=0, Max=100, Step=0.1, Decimals=1) float precipitation = 0.f;
    RM_PROPERTY(Edit, Label="雪にする", Tooltip="入れると、雨がだんだん小雨になってから雪に変わり、雪が強まって降水量に戻ります。外すと逆に、雪が弱まってから雨に (Play 中は 5 秒ほど。エディターではすぐその見た目)。雨と雪が一緒に降ることはありません。プラグイン「気候」があるとき (つなぎ「天候 × 気候」) は使わず、場所の気温で決まります") bool snowing = false;
    RM_PROPERTY(Edit, Label="積もる高さ (cm)", Tooltip="雪がこの高さまで積もります (cm、整数。最大 1000)。Play 中は降っている間に「積もる速さ」で。0: 積もらない。屋根の下には積もらず、やむと少しずつ溶けます (雨だと早く)", Min=0, Max=1000, Step=1, Decimals=0) float snowDepthCm = 10.f;
    RM_PROPERTY(Edit, Label="積もる速さ (cm / 分)", Tooltip="降水量 100 のとき、1 分に積もる高さ。降る雪の量に比例します (降水量 50 なら半分、10 なら 10 分の 1)。積もる高さに届いたら止まります。足跡も同じ速さで埋まります", Min=0, Max=1000, Step=1, Decimals=1) float snowSpeedCm = 20.f;
    RM_PROPERTY(Edit, Label="はじめから積もっている", Tooltip="入れると、雪の降る所には、Play の始めから「積もる高さ」いっぱいに雪が積もっています (エディターのシーンビューでも見えます)。外すと、雪の無い地面から、降るにつれて積もります") bool snowFromStart = true;
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
    // はじめから積もっている: 数え始めたフレームで、まだ雪を置いていない (場所ごとのときは、答える者の
    // マスが届いたフレームで置く)。
    RM_TRANSIENT() bool snowStartPending = false;
    // 場所の降り方 (WeatherPlace.h): 答える者 (つなぎ 天候 × 気候) が Play の間、カメラのいる所について
    // 毎フレーム書く - 降るもののうち雪の割合 (0 .. 1) と、降水量の倍率。placeSnow < 0: 書く者がいない
    // (雪にする で決める)。
    RM_TRANSIENT() float placeSnow = -1;
    RM_TRANSIENT() float placeMore = 1;
    // 場所ごとの濡れと積雪 (段階 2): 答える者が Play の間、毎フレーム、地図のマスごとの降り方を
    // placeCells に書く (PlaceCellsFor。最初のマスが placeFirstX / Z)。天候のシステムがそれを読んで
    // wetMap を進め、空にする (書く者がいない: 地図を使わず、シーン全体で 1 つ - wetWays)。
    RM_TRANSIENT() std::vector<WeatherPlace> placeCells;
    RM_TRANSIENT() int placeFirstX = 0;
    RM_TRANSIENT() int placeFirstZ = 0;
    // 場所ごとの雲 (気候の地図に塗った雲): 答える者 (つなぎ 天候 × 気候) が Play の間、毎フレーム書く。
    // placeClouds: カメラのいる所の雲の量 (0 .. 100。数秒かけて近づける)。< 0: 書く者がいない (雲の量のまま)。
    // 空の暗さ・日の光・雲の厚さはこれで決まる (TargetOf)。
    RM_TRANSIENT() float placeClouds = -1;
    // cloudCells: カメラのまわりの雲の地図 (kCloudMapCells x kCloudMapCells。行が z、z の小さい行から)。
    // 値はそのマスの真ん中の雲の量 (0 .. 100)。最初のマスの真ん中のワールドの x, z が cloudFirstX / Z。
    // 空: 書く者がいない (空の雲はどこも同じ量)。空の絵はこれで、向きごとに雲の量を変える。
    RM_TRANSIENT() std::vector<float> cloudCells;
    RM_TRANSIENT() float cloudFirstX = 0;
    RM_TRANSIENT() float cloudFirstZ = 0;
    RM_TRANSIENT() WetMap wetMap;
    // ゲームが動いている間に、風が雲・粒・ひらひらをどれだけ運んだか (ワールド単位: 風を足し
    // 合わせたもの - 風が変わっても跳ばない)。
    RM_TRANSIENT() float driftX = 0;
    RM_TRANSIENT() float driftZ = 0;
};

// フィールドをできあいの天候 (WeatherPreset) にする: 雲の量、降水量、雪にする、霧の量、雷、風の強さ
// (風の向きと積もる高さはそのまま)。新しい版の値: 何もしない。
void ApplyPreset(Weather& w, WeatherPreset preset);
// 雪にする がどこまで来たか (0: 雨 .. 1: 雪): ゲームが数えたとおり、エディターでは箱の言うとおり。
// (場所の降り方があるとき (placeSnow >= 0) は使わない: TargetOf はそちらで雨と雪に分ける。)
float SnowInOf(const Weather& w);
// 間の道のりで、降水量のうち雨として降る分 / 雪として降る分 (x: SnowInOf): 雨が弱まって霧雨になり
// (真ん中で 0.15)、そこから軽い雪になって増えていく - 両方が同時に降ることはない。
float RainShare(float x);
float SnowShare(float x);
// 場所の降り方 `place` で、降水量 `precipitation` (0 .. 1) のうち雨として / 雪として降るもの。
void PlacedShare(float precipitation, const WeatherPlace& place, float& rain, float& snow);
// 積もる高さ in metres.
float SnowMostOf(const Weather& w);
// 今の降水量 (0 .. 1。フェードの途中は緩急つき): 雨と雪に分ける前のもの (場所の降り方で、粒を用意する量)。
float PrecipitationNow(const Weather& w);
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
// 積もっている雪が今どれだけの速さで増えるか (1 秒あたり m、0: 雪ではない) - 降る雪の量に比例
// (量 1 で WeatherNow::snowSpeed: 天候の「積もる速さ」。既定は 1 分に 20 cm。2026-11-24: 前は
// 1 分に 2.5 cm で、ユーザーが遅いと言った)。足跡も同じ速さで埋まる。
float SnowFallRate(const WeatherNow& now);
// 絵が見せる雪の深さ、すべての向きまとめて: ゲームが数えたとおり (Play)、またはエディターでは
// 雪が降っている間は積もる高さいっぱいで。
float SnowCoverNow(const Weather& w);
// はじめから積もっている雪 (Weather::snowFromStart。Play の最初のフレームと、エディターの見た目)。
// シーン全体: 雪が降っていれば、雪の降る向きの積雪を `most` (積もる高さ) に (向きが無ければ足す)。
void StartSnow(std::vector<WetWay>& ways, const WeatherNow& now, float most);

// 場所ごとの濡れと積雪の地図 (WetMap)。
// `eye` のまわりの地図の最初のマス (ワールドのマス番号)。
void WetMapFirst(Vec3 eye, int& firstX, int& firstZ);
// 答える者 (つなぎ) が Play の間、毎フレーム呼ぶ: `eye` のまわりの地図のマスの真ん中 (高さは eye.y) の
// 降り方を `info` に聞いて、w.placeCells / placeFirstX / Z に書く。
void PlaceCellsFor(Weather& w, const game_production::Scene& scene, Vec3 eye, const WeatherPlaceInfo& info);
// 地図を、最初のマスが (firstX, firstZ) になるようにずらす。新しく入るマスは地図の一番近いマスの値。
// 地図がまだ無いときは、全部のマスが `ways` (シーン全体で 1 つの濡れ) の値で始まる。
void MoveWetMap(WetMap& map, std::span<const WetWay> ways, int firstX, int firstZ);
// `dt` ゲーム秒あとの地図: マスごとに、その場所の降り方 (`places`: マスと同じ並び) で StepWet と
// 同じように進める (雪の所だけ積もり、雨の所だけ濡れ、雨の所の雪は溶ける)。`now` は霧・風・向き、
// `precipitation` (PrecipitationNow) が分ける前の降水量。向き (`ways`) はどこかで降っている
// もので作り、全部のマスで乾いたら消す。そのあと `ways` の濡れと積雪を `eye` のいるマスの値にする
// (スクリプトの rm::Sky::Wetness など)。
void StepWetMap(WetMap& map, std::vector<WetWay>& ways, std::span<const WeatherPlace> places, const WeatherNow& now, float precipitation, float dt,
                float snowMost, Vec3 eye);
// はじめから積もっている雪の、場所ごと: 各マスに、その場所で降るもののうち雪の割合のぶん `most` に
// (雨の所・降らない所: なし。境目では割合のとおり薄い)。`map` は MoveWetMap で作ってあること。
void StartSnowMap(WetMap& map, std::vector<WetWay>& ways, std::span<const WeatherPlace> places, const WeatherNow& now, float precipitation,
                  float most);
// ワールドの (x, z) のマス (地図の外: 一番近いマス。地図が空: null)。
const WetCell* WetCellAt(const WetMap& map, float x, float z);
// 地図の中で一番深い積雪 (全部の向きを足したもの、m)。
float WetMapMostSnow(const WetMap& map);
// 描くための地図: 最初のマスの角 (ワールドの x z、m)・1 マス (m)・一辺のマス数 (0: 使わない)。
struct WetMapInfo {
    float x = 0, z = 0, cell = 0, cells = 0;
};
WetMapInfo WetMapInfoOf(const WetMap& map);
// テクスチャにする値 (幅 3 x kWetMapCells、高さ kWetMapCells、1 テクセル 4 つ: 向き 0 .. 3): 左から
// 日なたの濡れ・日陰の濡れ・積雪 (m) のブロック。行が z。地図が空: 空。
std::vector<float> WetMapTexels(const WetMap& map);
// 絵が見せる地図: ゲームが数えたとおり (Play)。Play の前と止めた後は無い。
const WetMap* WetMapNow(const Weather& w);

// システム 天候: ゲームが動いている間、フェードが進み、雪を降らせる が入り (または抜け)、
// 雨が届いた所が濡れ (そして乾き)、雪が積もり、風が雲と降るものを運ぶ。
void RunWeather(game_production::SystemContext& ctx);

}  // namespace rm::weather
