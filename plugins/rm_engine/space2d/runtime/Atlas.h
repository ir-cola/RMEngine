#pragma once
// 2D 空間 - アトラス (スプライトシート): 1 枚の画像を同じ大きさのセルの格子に切り、左から右、
// 上から下へ番号を振ったもの (0 = 左上)。スプライトは 1 つのセルを描き、アニメーションはセルを順に進む。
// 純粋 (I/O も描画もしない)。tests/TestSpace2d.cpp でテストする。

#include <cstdint>
#include <span>

namespace rm::space2d {

// 画像のピクセルの長方形 (左上から数える)。w か h が 0: なし。
struct PixelRect {
    uint32_t x = 0, y = 0, w = 0, h = 0;
    bool operator==(const PixelRect&) const = default;
};

// 幅 `width` x 高さ `height` の画像を `columns` x `rows` (どちらも 1 以上) に切ったときのセル `index`。
// 最後を超える (または 0 未満の) 番号は回って戻る。最後の列 / 行は、割り切れずに余った分を
// 引き受ける (画像の端を超えることはない)。
PixelRect CellOf(uint32_t width, uint32_t height, int columns, int rows, int index);

// `framesPerSecond` で `seconds` 秒再生したアニメーションのセル: `frames` のセルを順に (空: 0 .. cellCount - 1)。
// `loop` が false なら最後で止まる。
int AnimationCell(std::span<const int> frames, int cellCount, float framesPerSecond, double seconds, bool loop, int startStep = 0);
// 同じやり方での並びの中の位置 (0 .. 長さ - 1): `startStep` 歩ぶん進んだところから、さらに `seconds` 秒。
int AnimationStep(std::span<const int> frames, int cellCount, float framesPerSecond, double seconds, bool loop, int startStep = 0);

}  // namespace rm::space2d
