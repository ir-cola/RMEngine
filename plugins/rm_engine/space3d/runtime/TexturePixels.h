#pragma once
// 3D 空間 - GPU に送れる状態の絵。絵そのものと、その小さいコピー (ミップマップ: 遠くでちらつかない)。
// 各コピーは 1 つ前の半分で、1 x 1 まで続く。形は生の RGBA かブロック圧縮 (BlockCompress.h)。
// CPU で作る。どのスレッドでもよく、重い所が描画を止めない。描画スレッドにあるのは
// Renderer::UploadTexture だけ。純粋 (tests/TestSpace3d.cpp でテストする)。

#include "platform/Pixels.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rm::space3d {

// GPU でのテクセルの持ち方。BC のフォーマットは 4 x 4 テクセルを 1 ブロック (8 か 16 バイト) に持つ:
//   BC1  RGB (+ アルファ 1 ビット)  8 バイト  - 透ける所のない色
//   BC3  RGBA                      16 バイト  - アルファつきの色 (BC7 の無い GPU 用)
//   BC4  1 チャンネル               8 バイト  - 数値の灰色の絵 (.r で読む)
//   BC5  2 チャンネル              16 バイト  - 法線マップ (x, y。z は計算する)
//   BC7  RGBA、より高品質          16 バイト  - アルファつきの色 (フィーチャーレベル 11.0)
enum class TextureFormat : uint8_t { Rgba8, BC1, BC3, BC4, BC5, BC7 };

bool IsBlockCompressed(TextureFormat f);
size_t BlockBytes(TextureFormat f);  // 4 x 4 ブロックのバイト数 (Rgba8 は 0)
// レベルの 1 行のバイト数 (テクセルか、ブロックの) と、レベル全体のバイト数。
size_t RowPitch(TextureFormat f, uint32_t width);
size_t LevelBytes(TextureFormat f, uint32_t width, uint32_t height);
// フォーマットが持つチャンネル数 (4 / 3 / 1 / 2): シェーダーが読めるもの。
int ChannelsOf(TextureFormat f);

struct TexturePixels {
    // [0] = 絵。各レベルの `pixels` は LevelBytes(format, width, height) バイトを持つ: RGBA の
    // テクセル (PixelOrder::Rgba) かブロックが、1 行ずつ並ぶ。
    std::vector<platform::Image> levels;
    bool srgb = true;  // 色の絵 (光として平均し、リニアで読む) か数値
    TextureFormat format = TextureFormat::Rgba8;
};

// 色の絵は光として平均する (sRGB -> リニア -> 戻す、テーブルで)。小さいコピーが暗くならない
// ようにするため。数値 (とアルファ) はそのまま平均する。絵が空なら、レベルも空。
// 画像の取り込みの設定のうち小さいコピーに関するもの (ImageImport: ミップマップ・透明部分を保つ)。
struct MipOptions {
    bool mipmaps = true;     // false: 絵そのものだけ (1 レベル)
    float keepCoverage = 0;  // > 0: 小さい各コピーのアルファを拡大し、絵の中で「この値以上」のテクセルと
                             // 同じ数だけ残るようにする (切り抜きの葉やフェンスが薄れて消えない)
};
TexturePixels MakeTexturePixels(platform::Image rgba, bool srgb, const MipOptions& options = {});

// 絵のうち、アルファが `cutoff` (0 .. 1) 以上の割合 (0 .. 1)。
float AlphaCoverage(const platform::Image& rgba, float cutoff);

}  // namespace rm::space3d
