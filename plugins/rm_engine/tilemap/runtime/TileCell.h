#pragma once
// タイルマップ - マスの値とマス目の計算。純粋 (I/O も描画もしない)。tests/TestTilemap.cpp でテストする。
// 設計: docs/TILEMAP.md 3-4・4-1。
//
// マスの値 (32 ビットの数 1 つ):
//   ビット 0〜7    列 (絵の元の中)
//   ビット 8〜15   行
//   ビット 16〜23  絵の元の番号 (1〜255)。0 = 空のマス
//   ビット 24〜26  左右反転・上下反転・斜めの反転 (斜め + 反転で 90 度回す。Tiled と同じ考え)
//   ビット 27〜31  使わない (0)
//
// マス目: 層の中の平面 (層のワールド行列は呼ぶ側が掛ける)。マス (x, y) は [x, x+1) x [y, y+1) にマスの
// 大きさを掛けた四角。Y は上。区画 (チャンク) は 32 x 32 マス: 区画の番号 = floor(マス / 32) (負の場所も同じ式)。
// 区画の中のマスは、下の行から、左から右へ数える (0〜1023)。

#include "base/Math.h"
#include "core/paint/GridPaint.h"

#include <cstdint>

namespace rm::tilemap {

using paint::Cell;
using TileValue = uint32_t;

inline constexpr TileValue kFlipX = 1u << 24;
inline constexpr TileValue kFlipY = 1u << 25;
inline constexpr TileValue kFlipDiagonal = 1u << 26;
inline constexpr TileValue kFlipMask = kFlipX | kFlipY | kFlipDiagonal;
inline constexpr int kMaxTileColumn = 255, kMaxTileRow = 255;

// 絵の元 `source` (1〜255) の列 `column`・行 `row` (0〜255) のタイル。範囲の外: 0 (空)。
constexpr TileValue MakeTile(int source, int column, int row, TileValue flips = 0) {
    if (source < 1 || source > 255 || column < 0 || column > kMaxTileColumn || row < 0 || row > kMaxTileRow) return 0;
    return static_cast<TileValue>(column) | (static_cast<TileValue>(row) << 8) | (static_cast<TileValue>(source) << 16) | (flips & kFlipMask);
}
constexpr int SourceOf(TileValue v) { return static_cast<int>((v >> 16) & 0xFF); }
constexpr int ColumnOf(TileValue v) { return static_cast<int>(v & 0xFF); }
constexpr int RowOf(TileValue v) { return static_cast<int>((v >> 8) & 0xFF); }
constexpr TileValue FlipsOf(TileValue v) { return v & kFlipMask; }
constexpr bool IsEmpty(TileValue v) { return SourceOf(v) == 0; }

// --- マス目 (層の中の平面) ------------------------------------------------------------------
inline constexpr int kChunkSize = 32;
inline constexpr int kChunkCells = kChunkSize * kChunkSize;

// 0 や負の大きさを使える大きさに (グリッドの欄が 0 のとき: 1)。
Vec2 UsableCellSize(Vec2 cellSize);
// 平面の点 -> その点のあるマス (floor)。
Cell CellAt(Vec2 local, Vec2 cellSize);
// マスの左下の角と右上の角。
Vec2 CellMin(Cell c, Vec2 cellSize);
Vec2 CellMax(Cell c, Vec2 cellSize);
Vec2 CellCenter(Cell c, Vec2 cellSize);

// 平面の四角 [mn, mx] に触れるマスの範囲 (両端を含む)。
struct CellRange {
    Cell min, max;
};
CellRange CellsIn(Vec2 mn, Vec2 mx, Vec2 cellSize);

// 負の数でも下へ丸める割り算 (floor(a / b)、b > 0)。
constexpr int FloorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// マスのある区画の番号。
constexpr Cell ChunkOf(Cell c) { return {FloorDiv(c.x, kChunkSize), FloorDiv(c.y, kChunkSize)}; }
// 区画の中のマスの番号 (下の行から、左から右へ。0〜1023)。
constexpr int IndexInChunk(Cell c) {
    const Cell k = ChunkOf(c);
    return (c.y - k.y * kChunkSize) * kChunkSize + (c.x - k.x * kChunkSize);
}
// 区画 `chunk` の中の番号 `index` のマス。
constexpr Cell CellOfIndex(Cell chunk, int index) { return {chunk.x * kChunkSize + index % kChunkSize, chunk.y * kChunkSize + index / kChunkSize}; }

}  // namespace rm::tilemap
