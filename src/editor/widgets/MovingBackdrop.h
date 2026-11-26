#pragma once
// 動くテーマの絵 (2026-11-26。探検テーマパックとゲーミングが使う)。
//
// 止まった絵 (OrnamentPicture: 色の付いた形の並び) と違い、GPU で 1 フレームに 1 枚描く「光の絵」:
//   1. 場 (field): パックが HLSL で書いた関数 `float3 Field(float2 p)` を、ウィンドウの全部のピクセルで走らせる
//      (Direct3D 11 のピクセルシェーダー。p = ウィンドウの DIP の座標)。星雲のもや・光の筋・煙など、ぼんやりした物。
//      重いので画面より粗く (fieldScale 倍) 描き、画面の細かさに引き伸ばす。そのとき、パックの関数
//      `float3 Sharp(float2 p, float3 field)` が、くっきりした物 (惑星・山のかげ・ネオンの格子) を画面の細かさで
//      重ねる (+ 暗いグラデーションの縞が見えないように細かいざらつき = ディザ)。
//   2. 粒 (sprite): パックが CPU で並べた粒 (星・泡・火の粉・流れ星) を、その上に足す (加算)。粒の動きは
//      パックの純粋な関数 (テストあり)。粒の形 (やわらかい光・星・泡・尾) はここのシェーダー。
// 出来た絵は「黒の上の光」: テーマの地の色 (bg / panel) に足して描く (Direct2D の D2D1_PRIMITIVE_BLEND_ADD)。
// ウィンドウの背景には強さ 1 で、パネルとバーには弱く (pack が決める割合) 足すので、パネルの色と文字は残る。
//
// 1 フレームに何度も描かれる (背景、そして各パネルとバー) が、絵を作るのはウィンドウごとに 1 フレーム 1 回だけ
// (Context の時刻が同じ間は覚えた絵)。そのあとは 1 回の塗り (ビットマップのブラシで角の丸い四角) で済む。
// 描くと、次のフレームを `kFrameSeconds` 後に求める (Context::RequestFrameAfter: 最小化中・ほかのアプリが前に
// いる間は、ウィンドウが止める)。止まったテーマはこれを使わないので、余計に描き直さない。
//
// GPU のデバイスはウィンドウの描画先 (スワップチェーンの絵) からたどる。デバイスが作り直されたら作り直す。
// シェーダーは別のスレッドで作る (重い絵は 1 秒ほどかかる。画面を止めない)。出来るまでと、GPU で描けないとき
// (シェーダーを作れない など) は何も描かない (テーマの色だけになる)。

#include "ui/Types.h"

#include <d2d1_1.h>

#include <cstdint>
#include <vector>

namespace rm::ui {
class Context;
}

namespace rm::editor::w {

// 粒の形。
enum class SpriteShape : uint32_t {
    Glow = 0,    // やわらかい光の点 (半径 = 光の芯。まわりに 3 倍までにじむ)
    Star = 1,    // 星: 鋭い芯 + にじみ + 明るいものは 4 本の光の筋 (`spikes` の強さ)
    Bubble = 2,  // 泡: 光る縁と左上のつや、うすい中
    Streak = 3,  // 尾を引く光 (流れ星・速い火の粉): `at` が頭、`tail` が頭から尾の先へのずれ
};

struct BackdropSprite {
    Vec2 at;             // 中心 (ウィンドウの DIP)
    float radius = 1.f;  // 大きさ (DIP)
    Color color;         // 足す光 (rgb に a を掛けて足す)
    SpriteShape shape = SpriteShape::Glow;
    Vec2 tail{};         // Streak の尾
    float spikes = 0.f;  // Star の光の筋の強さ (0 .. 1)
};

// パックが 1 フレームごとに入れる物。
struct BackdropScene {
    // シェーダーの `look[0..3]` (float4 が 4 つ。何を入れるかは、パックの HLSL が決める: 惑星の場所など)。
    float look[16] = {};
    std::vector<BackdropSprite> sprites;
};

// 1 つの動く絵の作り方 (パックの絵 1 つに 1 つ。constexpr で置ける)。
struct MovingLook {
    const char* name;  // ログ用
    // HLSL: `float3 Field(float2 p)` (粗い場) と `float3 Sharp(float2 p, float3 field)` (画面の細かさ。くっきりした物を
    // 重ねる。何も無ければ field を返す) を書く。使える物: size (ウィンドウの DIP)、time (秒)、floating (1 = パネルの
    // ウィンドウ)、look[4] (BackdropScene::look)、fieldTexel (場の 1 ピクセル。UV)、pixelScale (1 DIP のピクセル数)、
    // 関数 Hash21 / Hash22 / Noise (勾配ノイズ) / Fbm / Hue (虹の色)。返す色は 0 .. 1 くらいの「足す光」(地の色に足す)。
    const char* hlsl;
    float fieldScale;  // 場を描く細かさ (画面のピクセルの何倍。0.5 = 縦横半分。ぼんやりした絵ほど小さくてよい)
    // 1 フレームごとの中身 (look と粒)。`time` = 秒 (最初に動く絵を描いた時から)。絵を作るときだけ呼ぶ。
    void (*fill)(BackdropScene& scene, const ui::Rect& window, double time, bool floating);
};

// 次のフレームまでの秒 (約 30 fps)。
inline constexpr double kFrameSeconds = 1.0 / 30.0;

// 動く絵の時刻 (秒。このアプリで最初に動く絵を描いた時から)。シェーダーと粒と同じ時計: ほかに動かす物
// (ゲーミングのパネルの縁の虹) をそろえるときに使う。
double MovingTime(const ui::Context& c);
// 背景: ウィンドウ全体に、光を強さ `strength` で足す (フレームの最初、地を塗った後)。次のフレームも求める。
void DrawMovingBackdrop(ui::Context& c, const MovingLook& look, bool floating, float strength = 1.f);
// パネルやバーの上: `r` (角の丸み `corner`) の中だけに、強さ `strength` で足す (同じフレームの同じ絵)。
void DrawMovingBackdropOn(ui::Context& c, const MovingLook& look, const ui::Rect& r, float corner, bool floating, float strength);

// --- 低い段 (Context なしで描く。絵を外で確かめる道具とテスト用) ---------------------------------------
// `dc` の描画先と同じ GPU で、`key` (ウィンドウを見分ける物) のこのフレームの絵を作る。同じ key と同じ time なら
// 作らずに前の絵。`sizeDip` = ウィンドウの大きさ。作れないときは null。dc は BeginDraw の中でよい (先に Flush する)。
ID2D1Bitmap1* RenderMovingLook(ID2D1DeviceContext* dc, const void* key, const MovingLook& look, Vec2 sizeDip, double time, bool floating);
// 作った絵を `r` (角の丸み `corner`) に、強さ `strength` で足す。
void AddMovingPicture(ID2D1DeviceContext* dc, ID2D1Bitmap1* picture, const ui::Rect& r, float corner, float strength);

}  // namespace rm::editor::w
