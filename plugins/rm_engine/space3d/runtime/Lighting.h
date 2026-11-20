#pragma once
// 3D 空間: 1 フレームの光とその影。GPU が読む数 (Renderer3D の定数バッファー `Lights` (b1) と
// `Shadows` (b2)) と、シャドウマップがどこから見るか。
// 純粋 (I/O も描画もしない)。tests/TestSpace3d.cpp でテストする。
//
//   std::vector<space3d::Light> lights = ...;   // シーンから (ゲームの 光 部品)
//   const space3d::LightSetup s = space3d::MakeLights(lights, defaultDirection, ambient);
//   // s.block -> GPU へ。s.shadowLight: 太陽のマップが誰の影を持つか (-1: なし)
//   // s.localViewProj[0 .. localSlices): 点光源・スポットライトのシャドウマップ
//
// 光がまったく無いとき: 強さ 1 の白い平行光が 1 つ、`defaultDirection` の向きに進み、影を落とす
// (光が無かった頃のシーンの見た目)。
// 描くのは多くても kMaxLights 個 (平行光も含む)。残りは外す (`dropped`)。
// どれを残すか (点光源・スポットライトのどれから順にシャドウマップを持つか): まず平行光 (全部を
// 照らす。与えられた順)、次に、カメラの位置で大事な点光源・スポットライト。明るい (色 x 強さ) ほど、
// 近い (目が届く範囲の外にどれだけ離れているか) ほど先 (LightWeight)。同じなら与えられた順。
//
// 影:
//   平行光        影を落とす最初の平行光。マップは正射影。落とすものが小さい間は、全部に合わせた
//                1 枚 (半径 kSingleMapRadius まで。カメラが動いても変わらない)。それより大きいと、
//                カメラが見るものに沿って分けた kCascades 枚 (カスケード: 近いものは鮮明、遠いものは
//                粗い)。kShadowDistance まで。
//   点光源        透視投影のマップ 6 枚 (まわりの立方体の面ごとに 1 枚: +X -X +Y -Y +Z -Z)。
//   スポット      円錐を通した透視投影のマップ 1 枚。
//   点光源とスポットは kLocalShadowSlices 枚のマップを分け合う。光の順に割り当て、入りきらない
//   光は影が無い (`shadowsLeftOut`)。
//
// ある点が受ける光の量 (シェーダー。Renderer3D.cpp の Shade。物理ベース、リニア):
//   環境光 (下向きの面は地面の色 .. 上向きの面は空の色、× その強さ)
//   + Σ 色 × 強さ × 減衰 × 影。素材に応じて拡散光と GGX のハイライトにする
// 減衰: 平行光は 1。点光源は (1 - 距離 / 範囲)²。スポットはさらに、円錐の縁から半角の 3/4 までに
// かけてなめらかに。

#include "base/Math.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <array>
#include <cstddef>
#include <optional>
#include <cstdint>
#include <span>

namespace rm::space3d {

enum class LightType : uint8_t { Directional, Point, Spot };

struct Light {
    LightType type = LightType::Directional;
    Vec3 position{0, 0, 0};    // 点光源・スポット (ワールド)
    Vec3 direction{0, -1, 0};  // 光が進む向き: 平行光・スポット (長さは何でもよい)
    Color color{1, 1, 1, 1};   // アルファは使わない
    float intensity = 1.f;     // 強さ (0: オフ)
    float range = 10.f;        // 点光源・スポット: 光が消える所 (ワールド単位)
    float spotAngle = 30.f;    // スポット: 円錐全体の角度 (度)
    bool castShadow = true;
};

// 光の当たり方に関係なくどの面も受ける光 (環境光): 上向きの面には空の色、下向きの面には地面の色、
// その間の面にはその中間 (光沢のある面ではその反射も)。ライトの色と同じくリニア。既定値は、設定
// できるようになる前の見た目 (上 0.16、下 0.08)。
struct Ambient {
    Color sky{0.16f, 0.16f, 0.16f, 1};
    Color ground{0.08f, 0.08f, 0.08f, 1};
    float intensity = 1.f;
};

inline constexpr size_t kMaxLights = 8;
inline constexpr size_t kLocalShadowSlices = 8;     // 点光源・スポットのシャドウマップの数 (点光源は 6 枚使う)
inline constexpr uint32_t kLocalShadowSize = 1024;  // 1 枚あたりのピクセル数
inline constexpr int kCascades = 3;
inline constexpr float kSingleMapRadius = 25.f;     // この大きさの球の中に影を落とすものが収まるなら、それに合わせた 1 枚のマップ
inline constexpr float kShadowDistance = 150.f;     // カスケードが目の前方に届く距離の上限

// GPU が読む定数バッファー `Lights` (float4 の並び、HLSL のパッキング)。
struct LightBlock {
    float info[4];                   // x: 光の数
    float ambientSky[4];             // rgb: 上からの環境光 (× その強さ)
    float ambientGround[4];          // rgb: ... 下からの環境光
    float position[kMaxLights][4];   // xyz: 位置 (点光源・スポット)、w: 種類 (0 平行光、1 点光源、2 スポット)
    float direction[kMaxLights][4];  // xyz: 光が進む向き (長さ 1)、w: 範囲
    float color[kMaxLights][4];      // rgb: 色 × 強さ、w: 1 = 影は太陽のマップ
    float spot[kMaxLights][4];       // x: 円錐の縁 (半角) の cos、y: 薄れ始める所の cos
    float shadow[kMaxLights][4];     // 点光源・スポット: x: 最初のシャドウマップ (-1: なし)。y: 光からの距離 1 あたり、面から
                                     // どれだけ離れて見るか (法線方向のずらし)。z: 距離 1 での深度バイアス (シェーダーが距離で
                                     // 割る。マップの深度は均等ではないため)
};
static_assert(sizeof(LightBlock) % 16 == 0);

struct LightSetup {
    LightBlock block{};
    int shadowLight = -1;            // 太陽のマップを持つ光の block 内の番号 (-1: なし)
    Vec3 shadowDirection{0, -1, 0};  // その光の向き (長さ 1)
    size_t dropped = 0;              // 外した光の数 (kMaxLights より多いとき)
    // 点光源・スポットのシャドウマップ: それぞれが見るもの (ワールド -> -1..1、深度 0..1)。
    std::array<Mat4, kLocalShadowSlices> localViewProj{};
    size_t localSlices = 0;
    size_t shadowsLeftOut = 0;       // 影が欲しいのに空いているマップが無かった点光源・スポットの数
};

LightSetup MakeLights(std::span<const Light> lights, Vec3 defaultDirection, const Ambient& ambient = {}, Vec3 eye = {0, 0, 0});
// 点光源・スポットが、`eye` にあるカメラにとってどれだけ大事か。明るさ (色の輝度 x 強さ) を、
// 目が届く範囲の外に離れているほど小さくする (1 / (1 + d²)、d = 距離 - 範囲)。
float LightWeight(const Light& light, Vec3 eye);

// スポットライトのシャドウマップ: 光の位置から円錐を通して見る (柔らかい縁のために少し広め)。
Mat4 SpotShadowMatrix(Vec3 position, Vec3 direction, float spotAngle, float range);
// 点光源の 6 枚のシャドウマップ。立方体の面で、+X, -X, +Y, -Y, +Z, -Z (柔らかい縁がつながる
// ように 90 度より少し広め)。シェーダーは |x| |y| |z| の最大のもので面を選ぶ。
std::array<Mat4, 6> PointShadowMatrices(Vec3 position, float range);

// 太陽のカスケードの終わり。目の前方の距離で、nearZ と farZ の間。目の近くほど密 (等間隔と等比で
// 増える間隔の混ぜ合わせ。最後は farZ)。
std::array<float, kCascades> CascadeSplits(float nearZ, float farZ);

// 1 フレームの太陽のシャドウマップ。
struct SunShadowPlan {
    int count = 0;                              // 0: 何も影を落とさない、1: 落とすものに合わせた 1 枚、kCascades
    std::array<Mat4, kCascades> viewProj{};     // ワールド -> マップ
    std::array<float, kCascades> splitFar{};    // それぞれが終わる所 (目の前方。1 枚のとき: 巨大)
    std::array<float, kCascades> texel{};       // ワールドでのテクセル 1 つの幅
    std::array<float, kCascades> depthRange{};  // 各マップの深さ (ワールド単位)
};
// `direction`: 光が進む向き。`casters`: 影を落とすものの箱。`view` / `projection`: カメラ。
// `mapSize`: ピクセル数。カスケードは、見える範囲の自分の担当部分を囲む球に合わせ、テクセルの
// 整数倍で動かすので、カメラが動いてもちらつかない。
SunShadowPlan PlanSunShadows(Vec3 direction, const Bounds& casters, const Mat4& view, const Mat4& projection, uint32_t mapSize);

// 太陽のマップを合わせる箱 (PlanSunShadows の `casters`)。動かないものはそのまま正確に。動く
// もの (Box / MeshDraw::moving) は、箱の辺の外へ広げるだけで、その広げ方は段階的 (大きさの 16 分の 1
// を 2 のべき乗にしたもの)。だから、ものをドラッグしても (一番外のものでも)、段を越えるときだけ
// マップの行列 (と、それに伴う、動かないものの保持した層) が変わり、毎フレームは変わらない。動く
// ものが無いか、動くものが箱の内側なら、マップは何も動かなかったときと同じ。
// 全部が動くとき: 動くものの箱を、その大きさの段で広げたもの。どちらでもないとき: nullopt。
std::optional<Bounds> SunFitBounds(const std::optional<Bounds>& still, const std::optional<Bounds>& moving);

// 向きの地図 (RenderFrame::mapDirections。天候: 雨と雪が降る向き): 目のまわりで、`direction` に
// 沿った各線上の、最初の形 (RenderFrame::mapShapes) の深度。それより先の粒は当たっている。
// 幅は kCoverSide で、テクセルはワールドに固定 (目が動いてもちらつかない)。範囲は、目の
// kCoverAbove 手前 (向きに沿って) から、その後ろ kCoverDepth まで。
inline constexpr uint32_t kCoverSize = 1024;
inline constexpr float kCoverSide = 80.f, kCoverAbove = 80.f, kCoverDepth = 300.f;
struct CoverPlan {
    Mat4 viewProj;  // ワールド -> x, y -1..1 (向きと直角)、深度 0..1 (向きに沿って)
    float range = kCoverDepth;  // 深度の 0..1 をワールド単位で
};
// `direction`: 降る向き (長さは何でもよい。なし: 真下)。
CoverPlan PlanCover(Vec3 eye, Vec3 direction);

}  // namespace rm::space3d
