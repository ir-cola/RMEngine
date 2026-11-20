#pragma once
// 3D 空間: 一番奥の空 (スカイボックス) をピクセルシェーダーで描く。どの空も - エンジン付属の
// もの (地球の空、宇宙、パノラマ画像、6 枚の絵) も、ユーザーが書くもの (.rmsky ファイル) も - 同じ
// もので、次の関数を持つ HLSL のコードになる。
//
//   float3 Sky(SkyInput i)   // `i.dir` の向きを見たときの光 (線形。光の色として)
//
// これをエンジンの部分 (AssembleSky) とつなぎ、Renderer が一番奥に描く (RenderFrame::sky)。
// コードが使えるもの (SkyInput、補助関数) は kSkyPrelude (SkyShader.cpp) と、新しい空の元になる
// ファイル (NewSkyCode) に書いてある。
//
// 渡される数値 (SkyConstants、定数バッファ b3) は、空を描く側が入れる (ゲームの 空:
// game_x_3d/runtime/Sky.h)。純粋 (テキストだけ)。tests/TestSky.cpp が全部の空をコンパイルする。

#include "base/Math.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rm::space3d {

// 定数バッファ `SkyFrame` (b3) を GPU が読む形にしたもの。Renderer が invViewProj
// (カメラの回転だけ: クリップ空間 -> ワールドでの向き) を入れ、残りは空のもの。
struct SkyConstants {
    float invViewProj[16];
    float sun[4];       // xyz: 太陽の向き (長さ 1)、w: 秒 (SkyInput::time)
    float moon[4];      // xyz: 月の向き、w: 時刻 0..24 (SkyInput::hour)
    float info[4];      // x: 明るさ (Sky の結果に掛ける)、yzw: 使わない
    float param[8][4];  // 空自身の数値 (組み込みの空の色、ユーザーの 値 と 色)
};
static_assert(sizeof(SkyConstants) % 16 == 0);
inline constexpr size_t kSkyParams = 8;
inline constexpr size_t kSkyTextures = 6;  // sky0 .. sky5 (t0 .. t5)

// 光 <-> 画面の数値 (sRGB)。レンダラーのシェーダーと空のもので同じ。
extern const char kColorHlsl[];

// 空の HLSL 全体: エンジンの部分、次に `code` (`#line 1 "<fileName>"` 付き: コンパイラの
// メッセージがそのファイルと自分の行を示す)、そして頂点シェーダー SkyVS とピクセルシェーダー
// SkyPS (ps_4_0 / vs_4_0)。
std::string AssembleSky(std::string_view code, std::string_view fileName);

// エンジン付属の空 (.rmsky と同じ形のコード)。
//   Earth     param[0] rgb: 太陽の光、a: 昼 (0 夜 .. 1)
//             param[1] x: 雲の量 0..1、y: その暗さ、z: もや、w: 今の雷の光 (0 .. 1)
//             param[2] rgb: 真上の空、a: 星 (0 .. 1)
//             param[3] rgb: 地平線の空、a: 月の光
//             param[4] rgb: 地平線より下の地面、a: 夕焼けのにじみ (0 .. 1)
//             param[5] xy: 風が雲をどれだけ運んだか (ワールド x, z)、zw: 今の風 (m/s)
//   Space     param[0] x: 星、y: 星雲、z: 天の川、w: 1 = 太陽を描く
//             param[1] rgb: 星雲の 1 つ目の色、param[2] rgb: その 2 つ目の色
//   Panorama  sky0: 周り 1 周の絵 (正距円筒: 360 x 180 度)、param[0] x: 回した量 (ラジアン)
//   SixFaces  sky0 .. sky5: +X (右)、-X (左)、+Y (上)、-Y (下)、+Z (後ろ)、-Z (前)、
//             param[0] x: 回した量 (ラジアン)
enum class BuiltinSky { Earth, Space, Panorama, SixFaces };
std::string_view BuiltinSkyCode(BuiltinSky sky);
const char* BuiltinSkyName(BuiltinSky sky);  // メッセージに出す「ファイル名」 ("地球の空")

// 新しい .rmsky ファイルの中身: 出発点になる短い空。コメントに、使えるものを書いてある。
std::string_view NewSkyCode();
inline constexpr char kSkyExtension[] = ".rmsky";

}  // namespace rm::space3d
