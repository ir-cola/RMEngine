#pragma once
// タイルマップ - 塗る計算 (筆・四角・消しゴム・スポイト)。どのマスを塗るかは核の core/paint/GridPaint
// (線・四角)、何を塗るかはここ (スタンプ: パレットで選んだ何マスかの四角)。純粋 - tests/TestTilemap.cpp。
// 設計: docs/TILEMAP.md 4-2。
//
// スタンプの並び: 上の行から、左から右へ (パレットの画像と同じ向き)。置くときはスタンプの左上をマスに合わせる
// (マス目は Y が上なので、スタンプの 1 行下はマスの y - 1)。

#include "rm_engine/tilemap/runtime/TileStore.h"

#include <set>
#include <utility>
#include <vector>

namespace rm::tilemap {

struct Stamp {
    int width = 0, height = 0;
    std::vector<TileValue> tiles;  // width * height。上の行から、左から右へ。0: そのマスは変えない (スポイトで取った空のマス)
    bool Empty() const { return width <= 0 || height <= 0 || tiles.size() != static_cast<size_t>(width) * static_cast<size_t>(height); }
    // 左から `i` 列目・上から `j` 行目 (範囲の外は回って戻る: 四角をスタンプで敷き詰める)。
    TileValue At(int i, int j) const;
};

// 絵の元 `source` の 列 `column`・行 `row` から `width` x `height` のタイルのスタンプ (パレットで選んだ四角)。
Stamp StampOfTiles(int source, int column, int row, int width, int height);
// 層の 2 つの角の間のマスを取ったスタンプ (スポイト)。空のマスは 0。
Stamp PickStamp(const TileStore& store, Cell a, Cell b);

// 触った区画を集める (書き直す区画: TileStore::WriteTo・Undo の命令)。
using ChunkSet = std::set<std::pair<int, int>>;  // (区画の x, 区画の y)
std::vector<Cell> ChunksOf(const ChunkSet& set);

// スタンプの左上をマス `at` に合わせて置く。変えたマスの区画を `touched` に。変わったら true。
bool PlaceStamp(TileStore& store, Cell at, const Stamp& stamp, ChunkSet& touched);
// 2 つの角の間の四角を、スタンプで敷き詰める (スタンプの左上を四角の左上に)。`outline`: 縁だけ。
// 大きすぎる (core/paint の上限) と塗らずに false。
bool FillRect(TileStore& store, Cell a, Cell b, const Stamp& stamp, bool outline, ChunkSet& touched);
// マスを空にする: スタンプの大きさの四角 (左上が `at`)。
bool EraseAt(TileStore& store, Cell at, int width, int height, ChunkSet& touched);
// 2 つの角の間を空にする。
bool EraseRect(TileStore& store, Cell a, Cell b, bool outline, ChunkSet& touched);

}  // namespace rm::tilemap
