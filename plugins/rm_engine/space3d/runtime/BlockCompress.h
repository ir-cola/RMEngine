#pragma once
// 3D 空間 - GPU がそのまま読める形に圧縮した画像 (BC = ブロック圧縮: 4 x 4 テクセルごとに 8 か 16 バイト -
// RGBA の 4 分の 1 から 8 分の 1 のメモリで、送るのも速い)。
// 純粋 (tests/TestSpace3d.cpp でテストする): 画像がどの形式になるかと、ブロックのコーデック。
//
//   BC1 / BC3 / BC4 / BC5   2 つの端の色 (または値) と、各テクセルがその間のどこにあるか
//   BC7                     モード 6 だけ: RGBA を通る 1 本の線で 16 段 (アルファも色と同じ細かさで
//                           残す)。全部ここで書く - ライブラリは使わない (理由は docs/TECHNOLOGIES.md)
//
// 遅い (4096 x 4096 の画像は数秒かかる): ワーカースレッドでだけ行い、結果はディスク (TextureFile.h) に
// 残すので、1 回で済む。

#include "core/asset/ImportSettings.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"

#include <cstdint>

namespace rm::space3d {

// 画像の用途 (シェーダーがどのチャンネルを、どう読むか)。
enum class TextureUse : uint8_t {
    Color,   // rgb (+ a): 色か発光の画像 (sRGB)
    Normal,  // xy (+ z): 法線マップ - BC5 は x と y を残し、シェーダーが z を求める
    Data,    // 数値 (metal / rough): 灰色なら 1 チャンネルで持ってよい (BC4。.rrr として読む)
};

struct PictureTraits {
    bool alpha = false;  // 透けているテクセルがある (alpha < 255)
    bool gray = false;   // 全テクセルが r = g = b (1 チャンネルで足りる)
};
PictureTraits TraitsOf(const platform::Image& rgba);

// `width` x `height` の画像 (一番上のレベル) の形式。4 の倍数でない大きさは圧縮しない (Direct3D 11 は
// 一番上のレベルが丸ごとのブロックであることを求める)。Auto: 色は BC1、アルファがあれば BC7
// (GPU に BC7 が無ければ BC3)。法線マップは BC5。灰色の数値は BC4、他の数値は BC1。用途が読めない設定
// (色に BC4 など) は Auto として扱う。BC7 が使えなければ BC3。
TextureFormat ChooseFormat(TextureUse use, asset::TextureCompression setting, PictureTraits traits, bool canBC7, uint32_t width,
                           uint32_t height);

// 4 x 4 の 1 ブロック: `rgba` は 16 テクセルを行ごとに並べたもの (64 バイト)。BC4 は 16 個の単独の値を
// 受け取り / 返す。BC5 は r と g を読む (復号すると b = 0, a = 255)。BC1 はアルファを無視する (復号すると 255)。
void EncodeBC1(const uint8_t rgba[64], uint8_t out[8]);
void DecodeBC1(const uint8_t in[8], uint8_t rgba[64]);
void EncodeBC4(const uint8_t values[16], uint8_t out[8]);
void DecodeBC4(const uint8_t in[8], uint8_t values[16]);
void EncodeBC5(const uint8_t rgba[64], uint8_t out[16]);
void DecodeBC5(const uint8_t in[16], uint8_t rgba[64]);
void EncodeBC3(const uint8_t rgba[64], uint8_t out[16]);
void DecodeBC3(const uint8_t in[16], uint8_t rgba[64]);
void EncodeBC7(const uint8_t rgba[64], uint8_t out[16]);  // モード 6
void DecodeBC7(const uint8_t in[16], uint8_t rgba[64]);   // モード 6 だけ (他: 赤紫)

// `format` の RGBA 画像の全レベル (4 の倍数でないレベルの端のブロックは、最後の行 / 列を繰り返す)。
// Rgba8 は画像をそのまま返す。
TexturePixels Compress(const TexturePixels& rgba, TextureFormat format);
// 1 つのレベルを RGBA に戻す (見るため、テストのため)。
platform::Image Decompress(const platform::Image& level, TextureFormat format);

}  // namespace rm::space3d
