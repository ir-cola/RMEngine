#pragma once
// マス目に塗る道具の計算 (どのマスを塗るか)。気候の地図・タイルマップ・後の地形が使う。どのソフトの
// 言葉も知らない。純粋 - tests/TestTilemap.cpp でテストする。
//
//   LineCells({0, 0}, {3, 1})   {0,0} {1,0} {2,1} {3,1}   (筆を速く動かしても、前のマスから今のマスまで飛ばない)
//   RectCells({0, 0}, {1, 1})   {0,0} {1,0} {0,1} {1,1}   (2 つの角の間の全部。下の行から、左から右へ)

#include <cstddef>
#include <vector>

namespace rm::paint {

struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell&) const = default;
};

// 一度に塗るマスの数の上限 (四角・後の塗りつぶし)。超えると塗らない (呼ぶ側が理由を出す)。
inline constexpr size_t kMaxPaintCells = 1'000'000;

// `a` から `b` まで、すき間なく通るマス (ブレゼンハムの線: 横・縦・斜めの隣へ 1 歩ずつ)。両端を含む。
std::vector<Cell> LineCells(Cell a, Cell b);
// 2 つの角 `a` と `b` の間の全部のマス (両方を含む)。`outline`: 縁のマスだけ。
// kMaxPaintCells を超えると空。
std::vector<Cell> RectCells(Cell a, Cell b, bool outline = false);
// 2 つの角の間のマスの数 (幅 x 高さ。大きくても溢れない)。
long long RectCount(Cell a, Cell b);

}  // namespace rm::paint
