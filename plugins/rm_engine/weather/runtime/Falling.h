#pragma once
// 天候 - 降るもの (雨と雪) と 濡れ を、GPU が描く形で (WeatherDraw.h): 数値と HLSL - 粒と雪片
// (それ専用の ExtraDraw) と、雨が届いた所を濡れて見せる、すべての面への働き (3D 空間の
// RenderFrame::surface)。どちらも 3D 空間の 向きの地図 (落ちる向きから見たコライダー:
// RendererShaders.h の MapPast) を読む。

#include "base/Math.h"
#include "rm_engine/weather/runtime/Weather.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rm::weather {

// 目のまわりに降る雨か雪: 目と一緒に動く箱の中の粒 / 雪片 (落ちる間、それぞれが世界での場所を
// 保つ。一方の面から出て、反対側から入る)。ソリッドなものの後に深度テストつきで描く。
// それぞれ頂点シェーダーが自分の番号から作る (バッファなし)。目の近くと箱の端では薄い。
struct Precipitation {
    enum class Kind : uint8_t { Rain, Snow };
    Kind kind = Kind::Rain;
    float amount = 0;              // 0 .. 1: なし .. kMaxRaindrops / kMaxSnowflakes (雨は、各粒がどれだけ重く見えるかも)
    float seconds = 0;             // 落ちる時間 (止まっているとき: そこで宙に浮く)
    Color light{1, 1, 1, 1};       // それらを照らす光 (リニア)
    float windX = 0, windZ = 0;    // 今の風 (m/s、ワールド)
    float driftX = 0, driftZ = 0;  // 風が運んだ距離 (ワールド単位)
    int map = -1;                  // 落ちる向きの 向きの地図 (-1: なし - 当たるものがない)
};
// (2026-11-20: 4 倍に - 一番強い雨が軽く感じた。嵐は雨の壁)
// (2026-11-20 また: 雨は減らす - どしゃぶりは粒より霧。雪はずっと多く)
inline constexpr uint32_t kMaxRaindrops = 40000;
inline constexpr uint32_t kMaxSnowflakes = 90000;
inline constexpr float kRainBox = 24.f;  // 箱の一辺 (ワールド単位)
inline constexpr float kSnowBox = 18.f;
// 風の流れはここまで来ると 0 からやり直す (ワールド単位): 両方の箱の倍数 (そこでは見た目が
// 同じ) で、雲の飛びがめったに起きないほど遠く (大きな嵐で: 何時間も)。
inline constexpr float kDriftWrap = kRainBox * kSnowBox * 400.f;
// 量に応じて何個描くか (0 .. 最大): 少しなら少なく (数粒: ぽつぽつ)、嵐に向かって量より
// 速く増える。
uint32_t PrecipitationCount(const Precipitation& p);

// どう落ちるか: 少なくとも `least` m/s で下へ (各粒 / 雪片はさらに最大 `spread` 速く)、横には
// 風の速さの `carried` だけ。シェーダーにこれらを渡す。
struct FallSpeed {
    float least, spread, carried;
};
FallSpeed FallOf(Precipitation::Kind kind);
// 風の中で平均してどちらへ行くか (長さ 1): 当たるものはそこから見る。
Vec3 FallDirection(Precipitation::Kind kind, Vec3 wind);

// 粒と雪片のシェーダー: FallVS / FallPS (vs_4_0 / ps_4_0)。3D 空間の kColorHlsl と
// kDirectionMapHlsl の後。その数値: FallConstants (b4)。
extern const char kFallHlsl[];
struct FallConstants {
    float viewProj[16];
    float eye[4];
    float info[4];   // x: 0 雨 / 1 雪、y: 秒、z: 箱の一辺、w: その 向きの地図 (-1: なし)
    float light[4];  // rgb: それらを照らす光 (リニア)
    float right[4];  // xyz: カメラの右 (ワールド)、w: 雨の強さ (0 .. 1: 粒の見た目)
    float up[4];     // xyz: カメラの上
    float wind[4];   // xy: 今の風 (m/s、ワールドの x z)、zw: 風が運んだ距離
    float fall[4];   // x: 最小の落下 (m/s)、y: それぞれがさらに速くなれる量、z: 風がどれだけ運ぶか
};
static_assert(sizeof(FallConstants) % 16 == 0);

// 濡れ と 積雪: すべての面への働き (3D 空間の RenderFrame::surface: Surface(...)) とその数値 -
// 雨と雪の降り方ごとに、それを見る 向きの地図 (`maps`、-1: なし - 全部開いている)、覆うものの
// 下へどれだけしみこむか、雪がどれだけ深く積もるか (float4 1 + k: 日なた、日陰、地図、
// しみこみ。float4 1 + kWetWays + k: x が雪)。
extern const char kWetSurfaceHlsl[];
std::vector<float> WetSurfaceData(std::span<const WetWay> ways, std::span<const int> maps);

// 高さのある 積雪 (RenderFrame::solids): SnowLayerVS / SnowLayerPS。
// RendererShaderText(kWetSurfaceHlsl) の後 - 目のまわりに、kSnowCell のマスを kSnowCells x
// kSnowCells 並べた格子 1 つを真上から見て、インデックスバッファで描く (頂点バッファなし)。
// 足跡: SnowTrailVS / SnowTrailPS (同じ方法): 目のまわりに、kTrailCell のマスを kTrailCells x
// kTrailCells 並べた絵。各フレームで前のフレームから描く (2 枚。R32G32B32A32_FLOAT)。
extern const char kSnowLayerHlsl[];
extern const char kSnowTrailHlsl[];
inline constexpr uint32_t kSnowCells = 384;
inline constexpr float kSnowCell = 0.125f;  // (48 m)
inline constexpr uint32_t kTrailCells = 512;
inline constexpr float kTrailCell = 0.09375f;  // (48 m)
struct SnowLayerConstants {
    float grid[4];       // xy: 格子の角 (ワールドの x z、ちょうどのマス単位)、z: 1 マス (m)、w: 1 辺のマス数
    float info[4];       // x: まっすぐ下の地図、y: その線が始まる所 (ワールドの y: 目)
    float snow[4][4];    // 各降り方: x: その雪 (m)、y: その 向きの地図 (-1: なし)
    float trail[4];      // 足跡: xy: 最初のマス (ワールドの x z)、z: 1 マス (m)、w: 1 辺のマス数 (0: なし)
    float trailMore[4];  // x: 足跡の数のうちどれか
};
static_assert(sizeof(SnowLayerConstants) % 16 == 0);
struct SnowTrailConstants {
    float trail[4];  // SnowLayerConstants::trail と同じ
    float more[4];   // x: 数のうちどれか、y: 今どれだけ埋まるか (m)、z: 上向きの線が始まる所 (ワールドの y)、w: 下からの地図
};
static_assert(sizeof(SnowTrailConstants) % 16 == 0);
// `eye` のまわりに積もる雪: `downMap` (まっすぐ下) が見るものの上に、`ways` (`maps` で見る) が
// 残した分の深さで。`trails` のとき足跡を読む。
SnowLayerConstants SnowLayerOf(Vec3 eye, std::span<const WetWay> ways, std::span<const int> maps, int downMap, uint32_t trailCount, bool trails);
// 足跡のフレーム: 前から `fill` m 埋まった。`upMap`: 動く形を下から見た地図。
SnowTrailConstants SnowTrailOf(Vec3 eye, uint32_t count, float fill, int upMap);

}  // namespace rm::weather
